#pragma once

#include <opencv2/core.hpp>
#include <optional>
#include <string>
#include <vector>

namespace fpga::vision {

// This optional module consumes CV_8UC1 grayscale, CV_8UC3 BGR or CV_8UC4 BGRA.
// Detector instances retain configuration/tracking state; use one per camera.
struct LaserLineParameters {
    int gray_threshold=30, min_line_width=3, max_line_width=7, min_inliers=100;
    double residual_threshold=2.0;
    std::string kernel_path;
};
struct LaserLineResult {
    double angle_deg=0, distance_px=0, valid_ratio=0, quality=0;
    std::optional<cv::Vec4f> line;
    std::vector<cv::Point2f> center_points;
    int inlier_count=0, total_count=0;
    std::string status="Lost";
    cv::Point2d image_center{};
};
class LaserLineDetector {
public:
    explicit LaserLineDetector(LaserLineParameters parameters={});
    LaserLineResult detect(const cv::Mat& image);
    void set_params(std::optional<int> gray_threshold={},std::optional<int> min_inliers={});
    void set_kernel(const cv::Mat& kernel);
    const LaserLineParameters& parameters() const { return parameters_; }
    static cv::Mat default_kernel();
private:
    LaserLineParameters parameters_;
    cv::Mat kernel_;
};

struct AlignmentParameters {
    int caliper_count=30, caliper_half_len=40, caliper_pitch=3, edge_min_gradient=5;
    int min_line_width=2,max_line_width=15;
    double gaussian_sigma=1.0,ransac_threshold=1.5,quality_threshold=0.3,pixel_scale_mm=0.0078;
    int search_padding=60,lost_threshold=5;
    bool debug=false;
};
struct LineResult {
    std::vector<cv::Point2f> points;
    cv::Vec4f line{};
    double angle_deg=0;
};
struct SubpixelPoints {
    std::vector<cv::Point2d> horizontal_pts,vertical_pts;
};
struct CrossResult {
    std::optional<LineResult> horizontal,vertical;
    std::optional<cv::Point2d> center;
    cv::Point2d image_center{};
    double offset_x=0,offset_y=0,center_error=0,dx_mm=0,dy_mm=0,total_offset_px=0;
    double angle_error=0,angle_h=0,angle_v=0,rotation=0,quality=0;
    std::string status="Lost";
    std::optional<SubpixelPoints> subpixel_data;
    std::optional<cv::Rect> roi_rect;
    std::optional<std::pair<cv::Vec4f,cv::Vec4f>> fit_lines;
};
class AlignmentDetector {
public:
    explicit AlignmentDetector(AlignmentParameters parameters={});
    CrossResult detect(const cv::Mat& image);
    void reset_tracking();
    void set_params(std::optional<int> scan_line_count={},
                    std::optional<int> edge_min_gradient={},
                    std::optional<int> max_line_width={});
    const AlignmentParameters& parameters() const { return parameters_; }
    bool is_tracking() const { return tracking_; }
    unsigned lost_count() const { return lost_count_; }
    static cv::Mat draw_overlay(const cv::Mat& image,const CrossResult& result);
private:
    CrossResult lost(cv::Point2d center,cv::Rect roi);
    AlignmentParameters parameters_;
    bool tracking_=false;
    std::optional<cv::Point2d> last_center_;
    unsigned lost_count_=0;
};

struct ChessboardResult {
    bool found=false;
    std::vector<cv::Point2f> corners;
    cv::Size pattern_size{9,6};
    double avg_distance=0,min_distance=0,max_distance=0,variance=0;
};
class ChessboardDetector {
public:
    explicit ChessboardDetector(cv::Size pattern_size={9,6},bool subpixel_refine=true);
    ChessboardResult detect(const cv::Mat& image) const;
    cv::Size pattern_size() const { return pattern_size_; }
    void set_pattern_size(cv::Size pattern_size);
    cv::Mat draw_corners(const cv::Mat& image,const ChessboardResult& result) const;
private:
    cv::Size pattern_size_;
    bool subpixel_refine_;
};

} // namespace fpga::vision
