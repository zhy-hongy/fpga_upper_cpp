#include "fpga/analysis.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fpga {
namespace {
void check_image(const Image& image) {
    if (!image.width || !image.height || std::uint64_t(image.width) * image.height != image.pixels.size())
        throw std::invalid_argument("analysis image dimensions must match Gray8 pixel buffer");
    if (image.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        image.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("analysis image dimensions exceed int32");
}
int reflect101(int coordinate, int length) {
    if (length == 1) return 0;
    while (coordinate < 0 || coordinate >= length) {
        if (coordinate < 0) coordinate = -coordinate;
        else coordinate = (length - 1) - (coordinate - (length - 1));
    }
    return coordinate;
}
// NumPy's float32 pairwise reduction keeps the focus metric compatible with
// cv2.Sobel(...CV_32F) followed by np.mean, rather than accumulating in double.
float pairwise_sum(const float* data, std::size_t count) {
    if (count < 8) {
        float value = -0.0f;
        for (std::size_t i = 0; i < count; ++i) value += data[i];
        return value;
    }
    if (count <= 128) {
        float partial[8];
        std::copy_n(data, 8, partial);
        std::size_t i = 8;
        for (; i < count - count % 8; i += 8)
            for (std::size_t j = 0; j < 8; ++j) partial[j] += data[i + j];
        float value = ((partial[0] + partial[1]) + (partial[2] + partial[3]))
                    + ((partial[4] + partial[5]) + (partial[6] + partial[7]));
        for (; i < count; ++i) value += data[i];
        return value;
    }
    std::size_t half = count / 2;
    half -= half % 8;
    return pairwise_sum(data, half) + pairwise_sum(data + half, count - half);
}
double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const double position = fraction * (values.size() - 1);
    const auto lo = static_cast<std::size_t>(position), hi = static_cast<std::size_t>(std::ceil(position));
    return values[lo] + (values[hi] - values[lo]) * (position - lo);
}
bool grid_equal(const std::vector<double>& a, const std::vector<double>& b, bool quantized = false) {
    if (a.size() != b.size()) return false;
    double tolerance = 1e-4, scale = 1.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return false;
        scale = std::max({scale, std::abs(a[i]), std::abs(b[i])});
    }
    if (quantized) {
        const auto value = static_cast<float>(scale);
        tolerance = std::max(tolerance, 2.0 * (std::nextafter(value, std::numeric_limits<float>::infinity()) - value));
    }
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > tolerance) return false;
    return true;
}
std::string row_status(std::size_t a, std::size_t b) {
    if (!a || !b) return "missing";
    return a == b ? "compared" : "count_mismatch";
}
void finish_stage(ErrorStats& stats, CompareDetail& detail, bool scalar,
                  std::vector<double> coordinates) {
    stats.dll_count = detail.dll_values.size(); stats.fpga_count = detail.fpga_values.size();
    stats.dll_valid_count = std::count(detail.dll_valid_mask.begin(), detail.dll_valid_mask.end(), true);
    stats.fpga_valid_count = std::count(detail.fpga_valid_mask.begin(), detail.fpga_valid_mask.end(), true);
    if (stats.status != "compared") return;
    const auto count = detail.dll_values.size();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    detail.coordinates = std::move(coordinates);
    detail.valid_pair_mask.resize(count);
    detail.signed_errors.assign(count, {nan, nan}); detail.errors.assign(count, nan);
    std::vector<double> errors;
    double sum = 0, squared = 0, bias = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto av = detail.dll_valid_mask[i], bv = detail.fpga_valid_mask[i];
        stats.dll_only_valid_count += av && !bv; stats.fpga_only_valid_count += !av && bv;
        stats.both_invalid_count += !av && !bv;
        detail.valid_pair_mask[i] = av && bv;
        if (!av || !bv) continue;
        const auto dx = scalar ? 0.0 : detail.fpga_values[i].x - detail.dll_values[i].x;
        const auto dz = detail.fpga_values[i].z - detail.dll_values[i].z;
        const double error = scalar ? std::abs(dz) : std::hypot(dx, dz);
        detail.signed_errors[i] = {dx, dz}; detail.errors[i] = error;
        errors.push_back(error); sum += error; squared += error * error; bias += dz;
        if (!stats.max_error_index || error > stats.max_error) {
            stats.max_error = error; stats.max_error_index = i;
            stats.max_error_x = detail.coordinates[i];
        }
    }
    stats.count = errors.size();
    if (errors.empty()) { stats.status = "no_valid_pairs"; return; }
    stats.min_error = *std::min_element(errors.begin(), errors.end());
    stats.mean_error = sum / errors.size(); stats.rms_error = std::sqrt(squared / errors.size());
    stats.signed_bias = bias / errors.size(); stats.p95_error = percentile(std::move(errors), 0.95);
}
std::optional<ColumnLagDiagnostic> column_lag(const AlgorithmData& reference, const CameraData& uploaded,
                                            const CompareDetail& detail) {
    const auto count = reference.centerline.size();
    if (count != uploaded.centerline.size() || count < 128) return std::nullopt;
    std::vector<std::size_t> indices;
    for (std::size_t i = 2; i < count - 2; ++i) {
        bool valid = detail.dll_valid_mask[i];
        for (int lag = -2; lag <= 2; ++lag) valid = valid && detail.fpga_valid_mask[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + lag)];
        if (valid) indices.push_back(i);
    }
    if (indices.size() < std::max<std::size_t>(128, count / 2)) return std::nullopt;
    std::array<std::pair<double, double>, 5> errors{};
    int best = -2;
    for (int lag = -2; lag <= 2; ++lag) {
        double absolute = 0, squared = 0;
        for (const auto index : indices) {
            const auto other = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + lag);
            const auto delta = uploaded.centerline[other].z - reference.centerline[index].z;
            absolute += std::abs(delta); squared += delta * delta;
        }
        errors[lag + 2] = {absolute / indices.size(), std::sqrt(squared / indices.size())};
        if (errors[lag + 2].first < errors[best + 2].first) best = lag;
    }
    const auto baseline = errors[2], shifted = errors[best + 2];
    if (best == 0 || baseline.first < 0.1 || shifted.first > 0.5 ||
        baseline.first < 4 * std::max(shifted.first, 1.0 / 65536.0) ||
        baseline.second < 3 * std::max(shifted.second, 1.0 / 65536.0)) return std::nullopt;
    return ColumnLagDiagnostic{best, indices.size(), baseline.first, baseline.second, shifted.first, shifted.second, false};
}

std::mutex native_mutex;
void* active_native = nullptr;
} // namespace

FocusDetector::FocusDetector(std::size_t history_length, std::size_t smooth_window)
    : history_length_(history_length), smooth_length_(smooth_window) {
    if (!history_length || !smooth_window) throw std::invalid_argument("focus history and smoothing lengths must be positive");
}
FocusResult FocusDetector::detect(const Image& image, std::optional<Roi> requested) {
    check_image(image);
    const int image_width = static_cast<int>(image.width), image_height = static_cast<int>(image.height);
    int x = 0, y = 0, width = image_width, height = image_height;
    if (requested) {
        x = std::clamp(requested->x, 0, image_width - 1); y = std::clamp(requested->y, 0, image_height - 1);
        width = std::clamp(requested->width, 1, image_width - x); height = std::clamp(requested->height, 1, image_height - y);
    }
    const auto count = std::size_t(width) * height;
    std::vector<float> gradients(count);
    double gray_sum = 0; std::size_t saturated = 0;
    auto pixel = [&](int col, int row) {
        return int(image.pixels[std::size_t(y + reflect101(row, height)) * image.width + x + reflect101(col, width)]);
    };
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col) {
            const int gx = -pixel(col - 1, row - 1) - 2 * pixel(col - 1, row) - pixel(col - 1, row + 1)
                           + pixel(col + 1, row - 1) + 2 * pixel(col + 1, row) + pixel(col + 1, row + 1);
            const int gy = -pixel(col - 1, row - 1) - 2 * pixel(col, row - 1) - pixel(col + 1, row - 1)
                           + pixel(col - 1, row + 1) + 2 * pixel(col, row + 1) + pixel(col + 1, row + 1);
            gradients[std::size_t(row) * width + col] = static_cast<float>(gx * gx + gy * gy);
            const auto gray = pixel(col, row); gray_sum += gray; saturated += gray >= 250;
        }
    smooth_.push_back(static_cast<double>(pairwise_sum(gradients.data(), count) / static_cast<float>(count)));
    if (smooth_.size() > smooth_length_) smooth_.pop_front();
    const auto score = std::accumulate(smooth_.begin(), smooth_.end(), 0.0) / smooth_.size();
    best_ = std::max(best_, score); history_.push_back(score);
    if (history_.size() > history_length_) history_.pop_front();
    return {score, best_, gray_sum / count, saturated * 100.0 / count, std::vector<double>(history_.begin(), history_.end())};
}
void FocusDetector::reset_peak() noexcept { best_ = 0; }
void FocusDetector::reset() noexcept { history_.clear(); smooth_.clear(); best_ = 0; }

SfComparison compare_sf(const AlgorithmData& reference, const CameraData& uploaded) {
    SfComparison result; result.notes = uploaded.notes;
    result.center.unit = "px"; result.pointcloud.unit = result.interpolation.unit = "mm";
    auto& cd = result.center_detail;
    std::vector<double> center_a, center_b;
    for (const auto& point : reference.centerline) {
        cd.dll_values.push_back({0, point.z}); cd.dll_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z) && point.z > 0); center_a.push_back(point.x);
    }
    for (const auto& point : uploaded.centerline) {
        cd.fpga_values.push_back({0, point.z}); cd.fpga_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z) && point.z > 0); center_b.push_back(point.x);
    }
    result.center.status = row_status(center_a.size(), center_b.size());
    if (result.center.status == "compared" && !grid_equal(center_a, center_b)) result.center.status = "grid_mismatch";
    finish_stage(result.center, cd, true, center_a);
    if (result.center.status == "compared" || result.center.status == "no_valid_pairs") {
        result.column_lag = column_lag(reference, uploaded, cd);
        if (result.column_lag) result.notes.push_back("COLUMN_LAG_SUSPECTED: uploaded centerline may be delayed; diagnostic only, original values and official errors preserved");
    }

    const bool a_columns = !reference.points3d.empty() && reference.points3d.size() == reference.centerline.size();
    const bool b_columns = !uploaded.points3d.empty() && uploaded.points3d.size() == uploaded.centerline.size();
    auto& pd = result.pointcloud_detail;
    for (std::size_t i = 0; i < reference.points3d.size(); ++i) {
        const auto& point = reference.points3d[i]; pd.dll_values.push_back({point.x, point.z});
        pd.dll_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z) && (!a_columns || cd.dll_valid_mask[i]));
    }
    for (std::size_t i = 0; i < uploaded.points3d.size(); ++i) {
        const auto& point = uploaded.points3d[i]; pd.fpga_values.push_back({point.x, point.z});
        pd.fpga_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z) && (!b_columns || cd.fpga_valid_mask[i]));
    }
    result.pointcloud.status = row_status(pd.dll_values.size(), pd.fpga_values.size());
    if (result.pointcloud.status == "compared" && a_columns && b_columns && result.center.status == "grid_mismatch")
        result.pointcloud.status = "grid_mismatch";
    std::vector<double> cloud_coordinates;
    for (std::size_t i = 0; i < reference.points3d.size(); ++i) cloud_coordinates.push_back(a_columns ? center_a[i] : double(i));
    if (result.pointcloud.status == "compared" && !(a_columns && b_columns)) result.notes.push_back("POINT_ORDER_ASSUMED: comparing uploaded point order without complete centerline column correspondence");
    finish_stage(result.pointcloud, pd, false, std::move(cloud_coordinates));

    auto& id = result.interpolation_detail;
    std::vector<double> interp_a, interp_b;
    for (const auto& point : reference.interpolated_3d) {
        id.dll_values.push_back({0, point.z}); id.dll_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z)); interp_a.push_back(point.x);
    }
    for (const auto& point : uploaded.interpolated) {
        id.fpga_values.push_back({0, point.z}); id.fpga_valid_mask.push_back(valid_numeric(point.x) && valid_numeric(point.z)); interp_b.push_back(point.x);
    }
    result.interpolation.status = row_status(interp_a.size(), interp_b.size());
    if (result.interpolation.status == "compared") {
        if (!grid_equal(reference.interpolation_x, interp_a, true) || !grid_equal(reference.interpolation_x, interp_b, true))
            result.interpolation.status = "grid_mismatch";
        else for (std::size_t i = 1; i < interp_a.size(); ++i)
            if (reference.interpolation_x[i] <= reference.interpolation_x[i - 1] || interp_b[i] <= interp_b[i - 1])
                result.interpolation.status = "grid_mismatch";
    }
    finish_stage(result.interpolation, id, true, reference.interpolation_x);
    for (const auto* stats : {&result.center, &result.pointcloud, &result.interpolation}) {
        if (stats->status == "grid_mismatch") result.notes.push_back("GRID_MISMATCH: different actual sampling coordinates; no resampling or residual calculation");
        else if (stats->status == "count_mismatch") result.notes.push_back("COUNT_MISMATCH: point counts differ; comparison is not truncated");
        else if (stats->status == "missing") result.notes.push_back("MISSING: stage data is absent; no error calculated");
        else if (stats->status == "no_valid_pairs") result.notes.push_back("NO_VALID_PAIRS: no jointly valid samples; zero count does not mean zero error");
        if (stats->validity_mismatch_count()) result.notes.push_back("VALIDITY_MISMATCH: reference-only and uploaded-only valid samples are counted separately");
    }
    result.notes.push_back("Error definition: center Y(px), point cloud X/Z Euclidean(mm), interpolation Z(mm); signed error = FPGA - reference");
    return result;
}

struct NativeLaserProfiler::Impl {
#ifdef _WIN32
    struct RawResult {
        float* center_points; int center_count; unsigned char* center_gray_values;
        float* original_points; int original_count; float* interpolated_points;
        int interpolated_count; int success; char error_msg[256];
    };
    using Init = int (__cdecl *)(const char*, const char*);
    using Extraction = int (__cdecl *)(float, int, int);
    using Interpolation = void (__cdecl *)(int, double, float, float, int, float, float);
    using Process = int (__cdecl *)(const unsigned char*, int, int, int, RawResult*);
    using Free = void (__cdecl *)(RawResult*);
    using Destroy = void (__cdecl *)();
    HMODULE module = nullptr;
    Init init = nullptr; Extraction extraction = nullptr; Interpolation interpolation = nullptr;
    Process process = nullptr, process_original = nullptr; Free free = nullptr; Destroy destroy = nullptr;
    template<class Function> Function symbol(const char* name, bool required = true) {
        auto raw = GetProcAddress(module, name);
        if (!raw && required) throw std::runtime_error(std::string("native DLL is missing export: ") + name);
        Function result = nullptr;
        static_assert(sizeof(result) == sizeof(raw), "Windows function pointer size mismatch");
        std::memcpy(&result, &raw, sizeof(result));
        return result;
    }
    ~Impl() { if (module) FreeLibrary(module); }
#endif
    bool initialized = false;
};

NativeLaserProfiler::NativeLaserProfiler(const std::string& dll_path) : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    auto path = std::filesystem::absolute(std::filesystem::u8path(dll_path));
    path.make_preferred();
    impl_->module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!impl_->module) throw std::runtime_error("cannot load native laser profiler DLL; Windows error=" + std::to_string(GetLastError()));
    impl_->init = impl_->symbol<Impl::Init>("LaserProfiler_Init");
    impl_->extraction = impl_->symbol<Impl::Extraction>("LaserProfiler_SetExtractionConfig", false);
    impl_->interpolation = impl_->symbol<Impl::Interpolation>("LaserProfiler_SetInterpolationConfig");
    impl_->process = impl_->symbol<Impl::Process>("LaserProfiler_Process");
    impl_->process_original = impl_->symbol<Impl::Process>("LaserProfiler_ProcessToOriginal");
    impl_->free = impl_->symbol<Impl::Free>("LaserProfiler_FreeResult");
    impl_->destroy = impl_->symbol<Impl::Destroy>("LaserProfiler_Destroy");
#else
    (void)dll_path;
    throw std::runtime_error("NativeLaserProfiler requires a Windows LaserProfiler DLL");
#endif
}
NativeLaserProfiler::~NativeLaserProfiler() { destroy(); }
void NativeLaserProfiler::initialize(const std::string& kernel_path, const std::string& neural_net_path) {
    std::lock_guard<std::mutex> lock(native_mutex);
#ifdef _WIN32
    if (active_native && active_native != impl_.get()) throw std::runtime_error("another native laser profiler is active; DLL state is process-global");
    if (impl_->initialized) { impl_->destroy(); impl_->initialized = false; active_native = nullptr; }
    if (!impl_->init(kernel_path.c_str(), neural_net_path.c_str())) throw std::runtime_error("native profiler initialization failed; verify kernel and neural network XML paths");
    impl_->initialized = true; active_native = impl_.get();
#else
    (void)kernel_path; (void)neural_net_path;
    throw std::runtime_error("native laser profiler is unavailable on this platform");
#endif
}
void NativeLaserProfiler::set_extraction_config(float accuracy_threshold, int min_width, int max_width) {
    if (!std::isfinite(accuracy_threshold) || accuracy_threshold <= 0 || min_width < 3 || min_width > max_width || max_width > 15)
        throw std::invalid_argument("invalid native extraction accuracy or line width");
    std::lock_guard<std::mutex> lock(native_mutex);
#ifdef _WIN32
    if (!impl_->initialized) throw std::runtime_error("native laser profiler is not initialized");
    if (!impl_->extraction) {
        if (accuracy_threshold == 1 && min_width == 3 && max_width == 7) return;
        throw std::runtime_error("legacy DLL does not support adjustable extraction settings");
    }
    if (!impl_->extraction(accuracy_threshold, min_width, max_width)) throw std::invalid_argument("native DLL rejected extraction settings");
#else
    throw std::runtime_error("native laser profiler is unavailable on this platform");
#endif
}
void NativeLaserProfiler::set_interpolation_config(const NativeInterpolationConfig& config) {
    const auto priority = static_cast<int>(config.priority);
    if (config.num_points < 2 || config.num_points > 1000000 || !std::isfinite(config.x_interval) || config.x_interval <= 0 ||
        !std::isfinite(config.threshold_x) || !std::isfinite(config.threshold_z) || config.threshold_x <= 0 || config.threshold_z <= 0 ||
        !std::isfinite(config.x_start) || !std::isfinite(config.x_end) || priority < 0 || priority > 3)
        throw std::invalid_argument("invalid native interpolation settings");
    std::lock_guard<std::mutex> lock(native_mutex);
#ifdef _WIN32
    if (!impl_->initialized) throw std::runtime_error("native laser profiler is not initialized");
    if (!impl_->extraction && config.num_points < 10) throw std::runtime_error("legacy DLL does not support fewer than ten interpolation points");
    impl_->interpolation(config.num_points, config.x_interval, config.threshold_x, config.threshold_z,
                         priority, config.x_start, config.x_end);
#else
    throw std::runtime_error("native laser profiler is unavailable on this platform");
#endif
}
AlgorithmData NativeLaserProfiler::process(const Image& image, int threshold, bool interpolate) {
    check_image(image);
    if (threshold < 0 || threshold > 255) throw std::invalid_argument("native gray threshold must be 0..255");
    std::lock_guard<std::mutex> lock(native_mutex);
#ifdef _WIN32
    if (!impl_->initialized) throw std::runtime_error("native laser profiler is not initialized");
    Impl::RawResult raw{};
    struct Guard { Impl* impl; Impl::RawResult* raw; ~Guard() { impl->free(raw); } } guard{impl_.get(), &raw};
    const auto start = std::chrono::steady_clock::now();
    const auto function = interpolate ? impl_->process : impl_->process_original;
    function(image.pixels.data(), static_cast<int>(image.height), static_cast<int>(image.width), threshold, &raw);
    if (!raw.success) {
        std::size_t length = 0; while (length < sizeof(raw.error_msg) && raw.error_msg[length]) ++length;
        throw std::runtime_error("native algorithm failed: " + std::string(raw.error_msg, length));
    }
    auto count_valid = [](int count, const void* pointer) {
        if (count < 0 || count > 1000000 || (count && !pointer)) throw std::runtime_error("native DLL returned invalid result counts or null buffers");
    };
    count_valid(raw.center_count, raw.center_points); count_valid(raw.original_count, raw.original_points);
    count_valid(raw.interpolated_count, raw.interpolated_points);
    AlgorithmData result;
    for (int i = 0; i < raw.center_count; ++i) result.centerline.push_back({raw.center_points[i * 2], raw.center_points[i * 2 + 1]});
    if (raw.center_gray_values && raw.center_count) result.center_grays.assign(raw.center_gray_values, raw.center_gray_values + raw.center_count);
    for (int i = 0; i < raw.original_count; ++i)
        result.points3d.push_back({raw.original_points[i * 3], raw.original_points[i * 3 + 1], raw.original_points[i * 3 + 2]});
    for (int i = 0; i < raw.interpolated_count; ++i) {
        result.interpolated_3d.push_back({raw.interpolated_points[i * 3], raw.interpolated_points[i * 3 + 1], raw.interpolated_points[i * 3 + 2]});
        result.interpolation_x.push_back(raw.interpolated_points[i * 3]);
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
#else
    (void)interpolate;
    throw std::runtime_error("native laser profiler is unavailable on this platform");
#endif
}
void NativeLaserProfiler::destroy() noexcept {
    std::lock_guard<std::mutex> lock(native_mutex);
#ifdef _WIN32
    if (impl_ && impl_->initialized) { impl_->destroy(); impl_->initialized = false; if (active_native == impl_.get()) active_native = nullptr; }
#endif
}

} // namespace fpga
