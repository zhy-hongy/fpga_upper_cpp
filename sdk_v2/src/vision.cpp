#include "fpga/vision.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace fpga::vision {
namespace {
constexpr double degrees=180.0/CV_PI;
cv::Mat gray_image(const cv::Mat& image) {
    if(image.empty()||image.depth()!=CV_8U||image.dims!=2)
        throw std::invalid_argument("vision input must be a nonempty 8-bit image");
    if(image.channels()==1)return image;
    cv::Mat gray;
    if(image.channels()==3)cv::cvtColor(image,gray,cv::COLOR_BGR2GRAY);
    else if(image.channels()==4)cv::cvtColor(image,gray,cv::COLOR_BGRA2GRAY);
    else throw std::invalid_argument("vision input must be grayscale, BGR, or BGRA");
    return gray;
}
cv::Mat bgr_image(const cv::Mat& image) {
    const auto gray=gray_image(image);
    cv::Mat out;
    if(image.channels()==1)cv::cvtColor(gray,out,cv::COLOR_GRAY2BGR);
    else if(image.channels()==4)cv::cvtColor(image,out,cv::COLOR_BGRA2BGR);
    else out=image.clone();
    return out;
}
double median(std::vector<float> values) {
    std::sort(values.begin(),values.end());
    const auto middle=values.size()/2;
    return values.size()%2?values[middle]:(double(values[middle-1])+values[middle])/2.0;
}
cv::Vec4f fit_line(const std::vector<cv::Point2f>& points) {
    cv::Vec4f line;
    cv::fitLine(points,line,cv::DIST_L2,0,0.01,0.01);
    return line;
}
double line_angle(const cv::Vec4f& line) { return std::atan2(double(line[1]),double(line[0]))*degrees; }

cv::Mat enhance(const cv::Mat& gray) {
    cv::Mat equalized,gx,gy;
    cv::createCLAHE(3.0,{8,8})->apply(gray,equalized);
    cv::Sobel(equalized,gx,CV_32F,1,0,3);
    cv::Sobel(equalized,gy,CV_32F,0,1,3);
    cv::Mat magnitude(gray.size(),CV_64F);
    double sum=0,maximum=0;
    for(int y=0;y<gray.rows;++y)for(int x=0;x<gray.cols;++x){
        const double a=gx.at<float>(y,x),b=gy.at<float>(y,x);
        const double value=std::sqrt(a*a+b*b);
        magnitude.at<double>(y,x)=value;sum+=value;
    }
    const double mean=sum/gray.total(),gain=mean>0?std::max(1.0,8.0/mean):1.0;
    for(int y=0;y<gray.rows;++y)for(int x=0;x<gray.cols;++x){
        auto& value=magnitude.at<double>(y,x);value*=gain;value*=value;maximum=std::max(maximum,value);
    }
    cv::Mat output;
    magnitude.convertTo(output,CV_32F,maximum>0?255.0/maximum:1.0);
    return output;
}
double projected_peak(const cv::Mat& map,bool horizontal) {
    const int length=horizontal?map.rows:map.cols;
    const int depth=horizontal?map.cols:map.rows;
    const double center=length/2.0,sigma=length/6.0;
    double maximum=-1;int best=0;
    for(int i=0;i<length;++i){
        // NumPy sums each float32 row/column before converting the projection to float64.
        float sum=0;
        for(int j=0;j<depth;++j)sum+=horizontal?map.at<float>(i,j):map.at<float>(j,i);
        const double weight=std::exp(-0.5*std::pow((i-center)/sigma,2));
        const double score=sum*weight;
        if(score>maximum){maximum=score;best=i;}
    }
    return best;
}
double subpixel(const std::vector<float>& values,int index) {
    if(index<=0||index>=int(values.size())-1)return index;
    const double left=values[index-1],middle=values[index],right=values[index+1];
    const double a=(left+right-2*middle)/2.0,b=(right-left)/2.0;
    if(std::abs(a)<1e-9)return index;
    return index+std::clamp(-b/(2*a),-1.0,1.0);
}
int peak_window(const std::vector<float>& gradient,int center,int half,double minimum) {
    const int lo=std::max(1,center-half),hi=std::min(int(gradient.size())-1,center+half);
    if(lo>=hi)return -1;
    const auto found=std::max_element(gradient.begin()+lo,gradient.begin()+hi);
    return *found<minimum?-1:static_cast<int>(found-gradient.begin());
}
struct ScoredPoint { cv::Point2f point;float score; };
struct Measurements { std::vector<ScoredPoint> centers;std::vector<cv::Point2d> edges; };
Measurements measure(const cv::Mat& gray,double rough_y,double rough_x,bool horizontal,
                     const AlignmentParameters& p) {
    Measurements result;
    for(int i=-p.caliper_count;i<=p.caliper_count;++i){
        const double fixed_value=(horizontal?rough_x:rough_y)+double(i)*p.caliper_pitch;
        const double start_value=(horizontal?rough_y:rough_x)-p.caliper_half_len;
        const double end_value=(horizontal?rough_y:rough_x)+p.caliper_half_len;
        if(fixed_value<0||fixed_value>=(horizontal?gray.cols:gray.rows)||start_value<0||end_value>=(horizontal?gray.rows:gray.cols))continue;
        const int fixed=static_cast<int>(fixed_value),start=static_cast<int>(start_value),end=static_cast<int>(end_value);
        cv::Mat profile(1,end-start+1,CV_32F);
        for(int j=start;j<=end;++j)
            profile.at<float>(0,j-start)=horizontal?gray.at<uint8_t>(j,fixed):gray.at<uint8_t>(fixed,j);
        if(p.gaussian_sigma>0){
            int size=static_cast<int>(4*p.gaussian_sigma+1)|1;
            size=std::max(3,std::min(size,profile.cols/2*2+1));
            // Preserve the Python implementation's column kernel applied to a 1-row profile.
            cv::filter2D(profile,profile,-1,cv::getGaussianKernel(size,p.gaussian_sigma));
        }
        std::vector<float> positive(profile.cols-1),negative(profile.cols-1);
        for(int j=0;j<profile.cols-1;++j){
            const float difference=profile.at<float>(0,j+1)-profile.at<float>(0,j);
            positive[j]=std::max(difference,0.0f);negative[j]=std::max(-difference,0.0f);
        }
        if(positive.size()<4)continue;
        const int search_half=std::min(p.caliper_half_len/2,20);
        const int left=peak_window(positive,p.caliper_half_len,search_half,p.edge_min_gradient);
        const int right=peak_window(negative,p.caliper_half_len,search_half,p.edge_min_gradient);
        if(left<0||right<0||left>=right||right-left<p.min_line_width||right-left>p.max_line_width)continue;
        const double left_sub=subpixel(positive,left),right_sub=subpixel(negative,right);
        const double center=start+(left_sub+right_sub)/2.0+0.5;
        const float score=(positive[left]+negative[right])/2.0f;
        result.centers.push_back({horizontal?cv::Point2f(float(fixed),float(center)):cv::Point2f(float(center),float(fixed)),score});
        result.edges.push_back(horizontal?cv::Point2d(fixed,start+left_sub+0.5):cv::Point2d(start+left_sub+0.5,fixed));
        result.edges.push_back(horizontal?cv::Point2d(fixed,start+right_sub+0.5):cv::Point2d(start+right_sub+0.5,fixed));
    }
    return result;
}
double numpy_random(std::mt19937& rng) {
    const auto a=rng()>>5,b=rng()>>6;
    return (double(a)*67108864.0+double(b))/9007199254740992.0;
}
std::vector<cv::Point2f> ransac(const std::vector<ScoredPoint>& scored,double threshold) {
    if(scored.size()<8)return {};
    const auto size=scored.size();
    std::vector<cv::Point2f> points;points.reserve(size);
    float sum=0;
    for(const auto& point:scored){points.push_back(point.point);sum+=point.score;}
    std::vector<double> probabilities(size),cdf(size);
    for(std::size_t i=0;i<size;++i)probabilities[i]=sum>0?float(scored[i].score/sum):1.0/size;
    auto make_cdf=[&](std::optional<std::size_t> excluded){
        double accumulated=0;
        for(std::size_t i=0;i<size;++i){if(!excluded||*excluded!=i)accumulated+=probabilities[i];cdf[i]=accumulated;}
        for(auto& value:cdf)value/=accumulated;
    };
    auto sample=[&](double uniform){return std::min<std::size_t>(std::upper_bound(cdf.begin(),cdf.end(),uniform)-cdf.begin(),size-1);};
    std::mt19937 rng(42);
    std::vector<cv::Point2f> best;
    const auto iterations=std::min<std::size_t>(100,size*(size-1)/2);
    for(std::size_t iteration=0;iteration<iterations;++iteration){
        make_cdf({});const auto first=sample(numpy_random(rng));auto second=sample(numpy_random(rng));
        if(first==second){make_cdf(first);second=sample(numpy_random(rng));}
        const auto one=points[first],two=points[second];
        const float dx=two.x-one.x,dy=two.y-one.y;
        if(std::abs(dx)<1e-6&&std::abs(dy)<1e-6)continue;
        std::vector<cv::Point2f> inliers;
        const bool horizontal=std::abs(dx)>std::abs(dy);
        const float slope=horizontal?dy/dx:dx/dy;
        const float intercept=horizontal?one.y-slope*one.x:one.x-slope*one.y;
        const double divisor=std::sqrt(double(slope)*slope+1);
        for(auto point:points){
            const float residual=horizontal?slope*point.x-point.y+intercept:slope*point.y-point.x+intercept;
            if(std::abs(residual)/divisor<threshold)inliers.push_back(point);
        }
        if(inliers.size()>best.size())best=std::move(inliers);
        if(best.size()==points.size())break;
    }
    return best.size()>=8?best:points;
}
std::optional<cv::Point2d> intersection(const cv::Vec4f& one,const cv::Vec4f& two) {
    const double determinant=double(one[0])*two[1]-double(one[1])*two[0];
    if(std::abs(determinant)<1e-12)return {};
    const double dx=double(two[2])-one[2],dy=double(two[3])-one[3];
    const double t=(dx*two[1]-dy*two[0])/determinant;
    return cv::Point2d(one[2]+t*one[0],one[3]+t*one[1]);
}
void validate(const AlignmentParameters& p) {
    if(p.caliper_count<1||p.caliper_count>10000||p.caliper_half_len<2||p.caliper_half_len>100000||
       p.caliper_pitch<1||p.caliper_pitch>100000||p.edge_min_gradient<0||p.min_line_width<1||
       p.max_line_width<p.min_line_width||!std::isfinite(p.gaussian_sigma)||p.gaussian_sigma<0||p.gaussian_sigma>1000||
       !std::isfinite(p.ransac_threshold)||p.ransac_threshold<=0||!std::isfinite(p.quality_threshold)||
       p.quality_threshold<0||p.quality_threshold>1||!std::isfinite(p.pixel_scale_mm)||p.pixel_scale_mm<0||
       p.search_padding<2||p.search_padding>100000||p.lost_threshold<1)
        throw std::invalid_argument("invalid alignment detector parameters");
}
}

cv::Mat LaserLineDetector::default_kernel() {
    const std::array<float,49> values{{
        .00304633309f,.00685271667f,.0111447871f,.0131059224f,.0111447871f,.00685271667f,.00304633309f,
        .00685271667f,.0154151646f,.0250701625f,.0294817332f,.0250701625f,.0154151646f,.00685271667f,
        .0111447871f,.0250701625f,.0407723859f,.0479470603f,.0407723859f,.0250701625f,.0111447871f,
        .0131059224f,.0294817332f,.0479470603f,.0563842505f,.0479470603f,.0294817332f,.0131059224f,
        .0111447871f,.0250701625f,.0407723859f,.0479470603f,.0407723859f,.0250701625f,.0111447871f,
        .00685271667f,.0154151646f,.0250701625f,.0294817332f,.0250701625f,.0154151646f,.00685271667f,
        .00304633309f,.00685271667f,.0111447871f,.0131059224f,.0111447871f,.00685271667f,.00304633309f}};
    return cv::Mat(7,7,CV_32F,const_cast<float*>(values.data())).clone();
}
LaserLineDetector::LaserLineDetector(LaserLineParameters parameters):parameters_(std::move(parameters)) {
    set_params(parameters_.gray_threshold,parameters_.min_inliers);
    if(parameters_.kernel_path.empty())kernel_=default_kernel();
}
void LaserLineDetector::set_params(std::optional<int> threshold,std::optional<int> minimum) {
    if(threshold){if(*threshold<0||*threshold>255)throw std::invalid_argument("invalid laser grayscale threshold");parameters_.gray_threshold=*threshold;}
    if(minimum){if(*minimum<2)throw std::invalid_argument("laser fit requires at least two points");parameters_.min_inliers=*minimum;}
}
void LaserLineDetector::set_kernel(const cv::Mat& kernel) {
    if(kernel.empty()||kernel.channels()!=1||kernel.dims!=2||!cv::checkRange(kernel))
        throw std::invalid_argument("laser filter kernel must be nonempty and finite");
    kernel.convertTo(kernel_,CV_32F);
}
LaserLineResult LaserLineDetector::detect(const cv::Mat& image) {
    const auto gray=gray_image(image);
    LaserLineResult result;result.image_center={gray.cols/2.0,gray.rows/2.0};
    if(gray.rows<3)return result;
    if(kernel_.empty()){
        try{cv::FileStorage storage(parameters_.kernel_path,cv::FileStorage::READ);if(storage.isOpened())storage["kernel"]>>kernel_;}
        catch(const cv::Exception&){return result;}
    }
    if(kernel_.empty())return result;
    cv::Mat filtered;cv::filter2D(gray,filtered,CV_8U,kernel_,{-1,-1},0,cv::BORDER_REFLECT_101);
    std::vector<cv::Point2f> points;
    for(int x=0;x<gray.cols;++x){
        int maximum=-1,row=0;
        for(int y=0;y<gray.rows;++y){const int value=filtered.at<uint8_t>(y,x);if(value>maximum){maximum=value;row=y;}}
        if(maximum<=parameters_.gray_threshold)continue;
        const int center=std::clamp(row,1,gray.rows-2);
        const double a=filtered.at<uint8_t>(center-1,x),b=filtered.at<uint8_t>(center,x),c=filtered.at<uint8_t>(center+1,x);
        const double denominator=a-2*b+c;
        const double delta=std::abs(denominator)>1e-9?std::clamp(0.5*(a-c)/denominator,-0.5,0.5):0;
        points.emplace_back(float(x),float(row+delta));
    }
    if(points.size()<std::size_t(parameters_.min_inliers))return result;
    auto line=fit_line(points);
    std::vector<float> distances;distances.reserve(points.size());
    for(auto point:points)distances.push_back(std::abs(line[1]*(point.x-line[2])-line[0]*(point.y-line[3])));
    // The Python implementation intentionally uses 2.0 here; its residual_threshold
    // constructor argument, like its width limits, is retained but not applied.
    const double threshold=std::max(2.0,median(distances)*3);
    for(std::size_t i=0;i<points.size();++i)if(distances[i]<=threshold)result.center_points.push_back(points[i]);
    if(result.center_points.size()<std::size_t(parameters_.min_inliers)){result.center_points.clear();return result;}
    line=fit_line(result.center_points);result.line=line;
    result.angle_deg=line_angle(line);
    while(result.angle_deg>90)result.angle_deg-=180;
    while(result.angle_deg< -90)result.angle_deg+=180;
    if(result.angle_deg>=90)result.angle_deg=90;
    double center_y=line[3];
    if(std::abs(line[0])>1e-9)center_y+=double(line[1])/line[0]*(result.image_center.x-line[2]);
    result.distance_px=center_y-result.image_center.y;
    result.inlier_count=static_cast<int>(result.center_points.size());result.total_count=static_cast<int>(points.size());
    result.valid_ratio=double(result.inlier_count)/result.total_count;result.quality=std::clamp(result.valid_ratio,0.0,1.0);
    result.status="Tracking";return result;
}

AlignmentDetector::AlignmentDetector(AlignmentParameters parameters):parameters_(std::move(parameters)) { validate(parameters_); }
void AlignmentDetector::set_params(std::optional<int> scans,std::optional<int> gradient,std::optional<int> width) {
    auto updated=parameters_;
    if(scans)updated.caliper_count=std::max(3,*scans);
    if(gradient)updated.edge_min_gradient=*gradient;
    if(width){if(*width<0||*width>33333)throw std::invalid_argument("alignment caliper width must be in 0..33333");updated.caliper_half_len=std::max(10,*width*3);}
    validate(updated);parameters_=updated;
}
void AlignmentDetector::reset_tracking() { tracking_=false;last_center_.reset();lost_count_=0; }
CrossResult AlignmentDetector::lost(cv::Point2d center,cv::Rect roi) {
    if(lost_count_<std::numeric_limits<unsigned>::max())++lost_count_;
    if(lost_count_>=static_cast<unsigned>(parameters_.lost_threshold)){tracking_=false;last_center_.reset();}
    CrossResult result;result.image_center=center;result.roi_rect=roi;return result;
}
CrossResult AlignmentDetector::detect(const cv::Mat& image) {
    const auto gray=gray_image(image);const cv::Point2d image_center(gray.cols/2.0,gray.rows/2.0);
    cv::Rect roi(0,0,gray.cols,gray.rows);
    if(tracking_&&last_center_){
        roi.x=std::max(0,static_cast<int>(last_center_->x-parameters_.search_padding));
        roi.y=std::max(0,static_cast<int>(last_center_->y-parameters_.search_padding));
        roi.width=std::min(gray.cols-roi.x,parameters_.search_padding*2);
        roi.height=std::min(gray.rows-roi.y,parameters_.search_padding*2);
    }
    if(roi.width<2||roi.height<2)return lost(image_center,roi);
    const auto map=enhance(gray(roi));
    const double rough_x=roi.x+projected_peak(map,false),rough_y=roi.y+projected_peak(map,true);
    auto horizontal=measure(gray,rough_y,rough_x,true,parameters_);
    auto vertical=measure(gray,rough_y,rough_x,false,parameters_);
    auto h_points=ransac(horizontal.centers,parameters_.ransac_threshold),v_points=ransac(vertical.centers,parameters_.ransac_threshold);
    if(h_points.size()<8||v_points.size()<8)return lost(image_center,roi);
    const auto h_line=fit_line(h_points),v_line=fit_line(v_points);
    const auto center=intersection(h_line,v_line);
    if(!center||center->x<0||center->x>=gray.cols||center->y<0||center->y>=gray.rows)return lost(image_center,roi);
    tracking_=true;last_center_=center;lost_count_=0;
    CrossResult result;result.center=center;result.image_center=image_center;
    result.offset_x=center->x-image_center.x;result.offset_y=center->y-image_center.y;
    result.center_error=std::hypot(result.offset_x,result.offset_y);result.total_offset_px=result.center_error;
    result.dx_mm=result.offset_x*parameters_.pixel_scale_mm;result.dy_mm=result.offset_y*parameters_.pixel_scale_mm;
    result.angle_h=line_angle(h_line);result.angle_v=line_angle(v_line);result.rotation=result.angle_h;
    if(result.rotation>45)result.rotation-=90;
    if(result.rotation< -45)result.rotation+=90;
    result.angle_error=std::abs(result.rotation);
    double h_gradient=0,v_gradient=0;
    for(const auto& point:horizontal.centers)h_gradient+=point.score;
    for(const auto& point:vertical.centers)v_gradient+=point.score;
    h_gradient/=horizontal.centers.size();v_gradient/=vertical.centers.size();
    const double gradient_score=std::min(1.0,(h_gradient+v_gradient)/60.0);
    const double ratio=(double(h_points.size())/horizontal.centers.size()+double(v_points.size())/vertical.centers.size())/2;
    result.quality=.3*gradient_score+.3*ratio+.2*std::max(0.0,1-result.center_error/100)+.2*std::max(0.0,1-result.angle_error/5);
    if(result.quality<parameters_.quality_threshold)return lost(image_center,roi);
    result.horizontal=LineResult{std::move(h_points),h_line,result.angle_h};
    result.vertical=LineResult{std::move(v_points),v_line,result.angle_v};
    result.subpixel_data=SubpixelPoints{std::move(horizontal.edges),std::move(vertical.edges)};
    result.fit_lines=std::make_pair(h_line,v_line);result.roi_rect=roi;result.status="Tracking";return result;
}
cv::Mat AlignmentDetector::draw_overlay(const cv::Mat& image,const CrossResult& result) {
    auto output=bgr_image(image);
    if(result.roi_rect)cv::rectangle(output,*result.roi_rect,{0,255,255},1);
    for(const auto* line:{&result.horizontal,&result.vertical})if(*line){
        for(auto point:(*line)->points)cv::circle(output,point,1,{0,255,0},-1);
        const auto value=(*line)->line;
        cv::line(output,{cvRound(value[2]-value[0]*10000),cvRound(value[3]-value[1]*10000)},
                       {cvRound(value[2]+value[0]*10000),cvRound(value[3]+value[1]*10000)},{0,0,255},1);
    }
    if(result.center)cv::drawMarker(output,*result.center,{255,0,255},cv::MARKER_CROSS,14,1);
    return output;
}

ChessboardDetector::ChessboardDetector(cv::Size pattern,bool refine):subpixel_refine_(refine) { set_pattern_size(pattern); }
void ChessboardDetector::set_pattern_size(cv::Size pattern) {
    if(pattern.width<2||pattern.height<2||pattern.width>1000||pattern.height>1000)
        throw std::invalid_argument("chessboard pattern must have 2..1000 internal corners per axis");
    pattern_size_=pattern;
}
ChessboardResult ChessboardDetector::detect(const cv::Mat& image) const {
    const auto gray=gray_image(image);cv::Mat small=gray;
    const double scale=std::max(gray.rows,gray.cols)>1024?1024.0/std::max(gray.rows,gray.cols):1;
    if(scale!=1)cv::resize(gray,small,{std::max(1,int(gray.cols*scale)),std::max(1,int(gray.rows*scale))},0,0,cv::INTER_AREA);
    ChessboardResult result;result.pattern_size=pattern_size_;
    const auto classic=[&](const cv::Mat& input){return cv::findChessboardCorners(input,pattern_size_,result.corners,cv::CALIB_CB_ADAPTIVE_THRESH|cv::CALIB_CB_NORMALIZE_IMAGE);};
    const auto sb=[&]{return cv::findChessboardCornersSB(small,pattern_size_,result.corners,cv::CALIB_CB_EXHAUSTIVE|cv::CALIB_CB_NORMALIZE_IMAGE);};
    if(pattern_size_.area()<=100)result.found=sb()||classic(small);
    else result.found=classic(small)||sb();
    if(!result.found){cv::Mat equalized;cv::equalizeHist(small,equalized);result.found=classic(equalized);}
    if(!result.found){result.corners.clear();return result;}
    if(scale!=1)for(auto& point:result.corners){point.x=float(point.x/scale);point.y=float(point.y/scale);}
    if(subpixel_refine_)cv::cornerSubPix(gray,result.corners,{5,5},{-1,-1},{cv::TermCriteria::EPS|cv::TermCriteria::MAX_ITER,30,.001});
    std::vector<float> distances;
    for(int row=0;row<pattern_size_.height;++row)for(int col=0;col<pattern_size_.width-1;++col)
        distances.push_back(float(cv::norm(result.corners[row*pattern_size_.width+col]-result.corners[row*pattern_size_.width+col+1])));
    for(int row=0;row<pattern_size_.height-1;++row)for(int col=0;col<pattern_size_.width;++col)
        distances.push_back(float(cv::norm(result.corners[row*pattern_size_.width+col]-result.corners[(row+1)*pattern_size_.width+col])));
    if(distances.empty())return result;
    const auto bounds=std::minmax_element(distances.begin(),distances.end());
    result.min_distance=*bounds.first;result.max_distance=*bounds.second;
    result.avg_distance=std::accumulate(distances.begin(),distances.end(),0.0)/distances.size();
    for(auto distance:distances)result.variance+=std::pow(distance-result.avg_distance,2);
    result.variance/=distances.size();return result;
}
cv::Mat ChessboardDetector::draw_corners(const cv::Mat& image,const ChessboardResult& result) const {
    auto out=image.clone();if(result.found)cv::drawChessboardCorners(out,result.pattern_size,result.corners,true);return out;
}

} // namespace fpga::vision
