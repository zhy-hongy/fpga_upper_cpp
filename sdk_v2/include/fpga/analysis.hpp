#pragma once

#include "fpga/data.hpp"

#include <deque>
#include <memory>

namespace fpga {

struct Roi { int x = 0; int y = 0; int width = 1; int height = 1; };
struct FocusResult {
    double score = 0;
    double best_score = 0;
    double mean_gray = 0;
    double saturation = 0; // Percentage >= 250.
    std::vector<double> history;
};
class FocusDetector {
public:
    explicit FocusDetector(std::size_t history_length = 200, std::size_t smooth_window = 5);
    FocusResult detect(const Image& image, std::optional<Roi> roi = std::nullopt);
    void reset_peak() noexcept;
    void reset() noexcept;
private:
    std::size_t history_length_, smooth_length_;
    std::deque<double> history_, smooth_;
    double best_ = 0;
};

struct AlgorithmData {
    std::vector<Point2> centerline;
    Bytes center_grays;
    std::vector<Point3> points3d;
    std::vector<Point3> interpolated_3d;
    std::vector<double> interpolation_x;
    double elapsed_ms = 0;
};
struct ErrorStats {
    std::string status = "missing";
    std::string unit;
    std::size_t dll_count = 0, fpga_count = 0;
    std::size_t dll_valid_count = 0, fpga_valid_count = 0;
    std::size_t dll_only_valid_count = 0, fpga_only_valid_count = 0, both_invalid_count = 0;
    std::size_t count = 0;
    double min_error = 0, max_error = 0, mean_error = 0, rms_error = 0, p95_error = 0, signed_bias = 0;
    std::optional<std::size_t> max_error_index;
    std::optional<double> max_error_x;
    std::size_t validity_mismatch_count() const noexcept { return dll_only_valid_count + fpga_only_valid_count; }
};
struct CompareDetail {
    std::vector<double> coordinates;
    std::vector<Point2> dll_values, fpga_values; // Selected components: Y, X/Z, or Z.
    std::vector<Point2> signed_errors; // First/last compared component; scalar stages use z.
    std::vector<double> errors; // Invalid pairs retain NaN.
    std::vector<bool> dll_valid_mask, fpga_valid_mask, valid_pair_mask;
};
struct ColumnLagDiagnostic {
    int lag_columns = 0;
    std::size_t common_count = 0;
    double original_mae_px = 0, original_rms_px = 0, shifted_mae_px = 0, shifted_rms_px = 0;
    bool applied = false; // Diagnostics never shift source values or comparison residuals.
};
struct SfComparison {
    ErrorStats center, pointcloud, interpolation;
    CompareDetail center_detail, pointcloud_detail, interpolation_detail;
    std::optional<ColumnLagDiagnostic> column_lag;
    std::vector<std::string> notes;
};
SfComparison compare_sf(const AlgorithmData& reference, const CameraData& uploaded);

enum class InterpolationPriority { Max = 0, Min = 1, Latest = 2, Earliest = 3 };
struct NativeInterpolationConfig {
    int num_points = 3200;
    double x_interval = 0.01;
    float threshold_x = 10, threshold_z = 10;
    InterpolationPriority priority = InterpolationPriority::Max;
    float x_start = 0, x_end = 0;
};

// Adapter to the existing upper-computer's LaserProfiler_* C ABI. Loading is
// explicit; users supply a DLL and its own XML assets. No OpenCV link dependency.
// The DLL owns process-global state: only one initialized adapter may be active.
// Calls and destruction are serialized, and native allocations are always freed
// by the DLL that created them after results are copied into SDK-owned vectors.
class NativeLaserProfiler {
public:
    explicit NativeLaserProfiler(const std::string& dll_path);
    ~NativeLaserProfiler();
    NativeLaserProfiler(const NativeLaserProfiler&) = delete;
    NativeLaserProfiler& operator=(const NativeLaserProfiler&) = delete;
    void initialize(const std::string& kernel_path, const std::string& neural_net_path);
    void set_extraction_config(float accuracy_threshold = 1.0f, int min_width = 3, int max_width = 7);
    void set_interpolation_config(const NativeInterpolationConfig& config);
    AlgorithmData process(const Image& image, int threshold = 30, bool interpolate = true);
    void destroy() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fpga
