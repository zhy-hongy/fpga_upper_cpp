#include "fpga/vision.hpp"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace fpga::vision;
namespace {
unsigned checks=0;
void check(bool valid,const char* label){++checks;if(!valid)throw std::runtime_error(label);}
cv::Mat laser(double slope=0){
    cv::Mat image(256,512,CV_8U);
    for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x){
        const double center=116+slope*(x-256),distance=(y-center)/1.8;
        image.at<uint8_t>(y,x)=static_cast<uint8_t>(12+220*std::exp(-distance*distance));
    }
    return image;
}
cv::Mat cross(){
    cv::Mat image(480,640,CV_8U,cv::Scalar(15));
    cv::line(image,{160,246},{317,246},{230},6,cv::LINE_8);
    cv::line(image,{353,246},{510,246},{230},6,cv::LINE_8);
    cv::line(image,{335,80},{335,228},{230},6,cv::LINE_8);
    cv::line(image,{335,264},{335,410},{230},6,cv::LINE_8);
    return image;
}
cv::Mat board(cv::Size inner={9,6},int square=50){
    cv::Mat image((inner.height+1)*square+100,(inner.width+1)*square+100,CV_8U,cv::Scalar(255));
    for(int row=0;row<=inner.height;++row)for(int col=0;col<=inner.width;++col)
        if((row+col)%2==0)cv::rectangle(image,{50+col*square,50+row*square,square,square},{0},-1);
    return image;
}
void save(const std::filesystem::path& file,const cv::Mat& image){
    std::ofstream output(file,std::ios::binary);
    output<<"P5\n"<<image.cols<<' '<<image.rows<<"\n255\n";
    for(int row=0;row<image.rows;++row)output.write(reinterpret_cast<const char*>(image.ptr(row)),image.cols);
}
void references(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::cout<<std::setprecision(17);
    LaserLineDetector detector;
    for(int index=0;index<3;++index){
        const auto image=laser(index==0?0:index==1?.075:-.05);const auto result=detector.detect(image);
        const auto file=directory/("laser"+std::to_string(index)+".pgm");save(file,image);
        std::cout<<"L "<<file.filename().string()<<' '<<result.status<<' '<<result.angle_deg<<' '<<result.distance_px<<' '
                 <<result.inlier_count<<' '<<result.total_count<<' '<<result.valid_ratio<<' '<<result.quality<<'\n';
    }
    AlignmentDetector alignment;const auto cross_image=cross();save(directory/"cross.pgm",cross_image);
    for(int index=0;index<2;++index){
        const auto result=alignment.detect(cross_image);
        if(!result.center)throw std::runtime_error("reference cross not detected");
        std::cout<<"C cross.pgm "<<result.status<<' '<<result.center->x<<' '<<result.center->y<<' '
                 <<result.offset_x<<' '<<result.offset_y<<' '<<result.rotation<<' '<<result.quality<<' '
                 <<result.horizontal->points.size()<<' '<<result.vertical->points.size()<<'\n';
    }
    for(int index=0;index<3;++index){
        const cv::Size pattern=index==2?cv::Size(11,11):cv::Size(9,6);
        auto image=board(pattern,index==2?40:50);
        if(index==1)cv::resize(image,image,{},2.2,2.2,cv::INTER_NEAREST);
        const auto file=directory/("board"+std::to_string(index)+".pgm");save(file,image);
        const auto result=ChessboardDetector(pattern).detect(image);
        std::cout<<"B "<<file.filename().string()<<' '<<pattern.width<<' '<<pattern.height<<' '<<result.found<<' '
                 <<result.corners.size()<<' '<<result.avg_distance<<' '<<result.min_distance<<' '
                 <<result.max_distance<<' '<<result.variance<<'\n';
    }
}
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--reference"){references(std::filesystem::u8path(argv[2]));return 0;}
    LaserLineDetector laser_detector;
    check(LaserLineDetector::default_kernel().rows==7,"laser default SF kernel");
    auto line=laser_detector.detect(laser());check(line.status=="Tracking"&&line.line,"laser tracking");
    check(line.inlier_count==512&&line.total_count==512,"laser center counts");
    check(std::abs(line.angle_deg)<.01&&std::abs(line.distance_px+12)<.1,"laser horizontal angle and signed distance");
    line=laser_detector.detect(laser(.075));check(std::abs(line.angle_deg-std::atan(.075)*180/CV_PI)<.1,"laser sloped fit");
    check(laser_detector.detect(cv::Mat::zeros(256,512,CV_8U)).status=="Lost","blank laser lost");
    cv::Mat bgr;cv::cvtColor(laser(),bgr,cv::COLOR_GRAY2BGR);check(laser_detector.detect(bgr).inlier_count==512,"BGR laser input");
    bool rejected=false;try{laser_detector.detect({});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"empty vision input rejected");
    AlignmentDetector alignment;auto found=alignment.detect(cross());
    check(found.status=="Tracking"&&found.center&&found.horizontal&&found.vertical,"separated cross tracking");
    check(std::abs(found.center->x-335)<.1&&std::abs(found.center->y-246)<.1,"cross subpixel center");
    check(std::abs(found.offset_x-15)<.1&&std::abs(found.offset_y-6)<.1,"cross offset signs");
    check(std::abs(found.dx_mm-found.offset_x*.0078)<1e-12,"cross physical pixel scale");
    check(found.subpixel_data&&found.fit_lines&&found.roi_rect->width==640,"cross fit and edge diagnostics");
    found=alignment.detect(cross());check(found.roi_rect->width==120&&found.status=="Tracking","cross local tracking ROI");
    for(int n=0;n<5;++n)check(alignment.detect(cv::Mat::zeros(480,640,CV_8U)).status=="Lost","cross consecutive loss");
    check(!alignment.is_tracking()&&alignment.lost_count()==5,"five losses restore full search");
    found=alignment.detect(cross());check(found.status=="Tracking"&&found.roi_rect->width==640,"cross reacquisition");
    check(AlignmentDetector::draw_overlay(cross(),found).channels()==3,"cross overlay");
    alignment.reset_tracking();check(!alignment.is_tracking()&&alignment.lost_count()==0,"cross tracking reset");
    ChessboardDetector chess;
    auto corners=chess.detect(board());check(corners.found&&corners.corners.size()==54,"default nine by six chessboard");
    check(std::abs(corners.avg_distance-50)<.01&&corners.variance<.01,"chessboard distance statistics");
    check(chess.draw_corners(board(),corners).size()==board().size(),"chessboard overlay dimensions");
    auto large=board();cv::resize(large,large,{},2.2,2.2,cv::INTER_NEAREST);corners=chess.detect(large);
    check(corners.found&&std::abs(corners.avg_distance-110)<.1,"large image downscale and full-resolution refinement");
    chess.set_pattern_size({11,11});corners=chess.detect(board({11,11},40));
    check(corners.found&&corners.corners.size()==121,"large pattern classic detection branch");
    std::cout<<"Vision: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<'\n';return 1;}}
