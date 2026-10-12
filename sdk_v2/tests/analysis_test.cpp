#include "fpga/analysis.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace fpga;
namespace {
void require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
void invalid(const std::function<void()>& function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}
void put_double(Bytes& out, double value) {
    std::uint64_t bits; std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(bits >> (56 - i * 8)));
}
std::uint32_t crc(const std::vector<Point2>& points) {
    Bytes out; for (const auto& point : points) { put_double(out, point.x); put_double(out, point.z); }
    return firmware_crc32(out);
}
std::uint32_t crc(const std::vector<Point3>& points) {
    Bytes out; for (const auto& point : points) { put_double(out, point.x); put_double(out, point.y); put_double(out, point.z); }
    return firmware_crc32(out);
}

void focus_test() {
    Image image{6, 4, Bytes(24)};
    for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = static_cast<std::uint8_t>(i * 37);
    FocusDetector detector;
    const auto first = detector.detect(image);
    require(first.score == 153600 && first.mean_gray == 105.5 && first.saturation == 0, "Sobel/Tenengrad matches Python golden metric");
    const auto second = detector.detect(image, Roi{1, 1, 3, 2});
    require(second.score == 84576 && second.best_score == 153600 && second.history.size() == 2, "focus ROI is reflected within crop and smoothed");
    const auto third = detector.detect(image, Roi{-3, 99, -1, 2});
    require(third.score == 56384 && third.mean_gray == 154, "focus invalid ROI is clamped like Python");
    detector.reset_peak(); require(detector.detect(image).best_score == 80688, "peak reset retains rolling smoothing history");
    detector.reset(); require(detector.detect(Image{2, 1, {255, 255}}).score == 0, "focus reset clears history and single-row reflect border is valid");
    require(detector.detect(Image{2, 1, {255, 255}}).saturation == 100, "focus saturation percentage");
    invalid([&] { detector.detect(Image{2, 2, {0}}); });
    invalid([] { FocusDetector detector(0); });
    FocusDetector short_history(2, 1);
    short_history.detect(image); short_history.detect(image);
    require(short_history.detect(image).history.size() == 2, "focus bounded history");
}
void comparison_test() {
    AlgorithmData reference;
    CameraData uploaded;
    const double ref_y[]{10, 0, 12, 0, 0, 10}, fpga_y[]{10, -999, -999, 13, -999, 10};
    const double ref_z[]{0, -999.999, 172, -999.999, -999.999, 170}, fpga_z[]{0, -999, -999, 173, -999, 170};
    for (std::size_t i = 0; i < 6; ++i) {
        reference.centerline.push_back({double(i), ref_y[i]}); uploaded.centerline.push_back({double(i), fpga_y[i]});
        const auto x = -2 + i * 0.5;
        reference.interpolated_3d.push_back({x, 0, ref_z[i]}); reference.interpolation_x.push_back(x);
        uploaded.interpolated.push_back({x, fpga_z[i]});
    }
    reference.points3d = {{1,99,170},{-999,-999,-999},{2,99,172},{-999,-999,-999},{51,99,191},{0,99,0}};
    uploaded.points3d = {{1,0,170},{-999,0,-999},{-999,0,-999},{3,0,173},{50,0,190},{0,0,0}};
    const auto result = compare_sf(reference, uploaded);
    for (const auto* stage : {&result.center, &result.pointcloud, &result.interpolation}) {
        require(stage->status == "compared" && stage->count == 2 && stage->max_error == 0, "only jointly valid pairs compared");
        require(stage->dll_valid_count == 3 && stage->fpga_valid_count == 3, "legacy/new invalid sentinel compatibility");
        require(stage->dll_only_valid_count == 1 && stage->fpga_only_valid_count == 1 && stage->both_invalid_count == 2, "validity mismatch accounting");
    }
    require(result.interpolation_detail.valid_pair_mask == std::vector<bool>({true,false,false,false,false,true}), "invalid residual mask and true height zero");
    require(std::isnan(result.interpolation_detail.errors[1]) && std::isnan(result.interpolation_detail.signed_errors[1].z), "invalid pair residuals remain NaN");
    auto mismatch = uploaded; mismatch.interpolated[0].x += 0.5;
    const auto grids = compare_sf(reference, mismatch);
    require(grids.interpolation.status == "grid_mismatch" && grids.interpolation.count == 0 && grids.interpolation_detail.errors.empty(), "different grids never resampled or compared");
    mismatch = uploaded; mismatch.centerline.pop_back();
    require(compare_sf(reference, mismatch).center.status == "count_mismatch", "count mismatch is not silently truncated");
    mismatch = uploaded; mismatch.centerline[0].x = 1;
    const auto columns = compare_sf(reference, mismatch);
    require(columns.center.status == "grid_mismatch" && columns.pointcloud.status == "grid_mismatch", "point cloud column correspondence follows center grid");
    require(compare_sf({}, {}).center.status == "missing", "missing stage is explicit");
    AlgorithmData invalid_reference; invalid_reference.centerline = {{0,0},{1,0}};
    CameraData invalid_uploaded; invalid_uploaded.centerline = {{0,-999},{1,-999}};
    const auto none = compare_sf(invalid_reference, invalid_uploaded);
    require(none.center.status == "no_valid_pairs" && none.center.count == 0, "zero compared pairs does not imply zero error");

    AlgorithmData lag_ref; CameraData lag_fpga;
    for (std::size_t i = 0; i < 256; ++i) {
        const double ref = 50 + 5 * std::sin(i * 0.21), other = 50 + 5 * std::sin((double(i) - 1) * 0.21);
        lag_ref.centerline.push_back({double(i), ref}); lag_fpga.centerline.push_back({double(i), other});
    }
    const auto lag = compare_sf(lag_ref, lag_fpga);
    require(lag.column_lag && lag.column_lag->lag_columns == 1 && !lag.column_lag->applied, "lag is diagnosed with common columns, not applied");
    require(lag.center.mean_error > 0.1, "official errors keep original sample ordering despite lag diagnosis");
}
void native_probe(const std::string& dll, const std::string& kernel, const std::string& net, const std::string& path) {
    const auto frame = load_binary_file(path);
    require(frame.size() > 18, "capture is truncated");
    const auto camera = decode_camera_data(Bytes(frame.begin() + 14, frame.end() - 4), frame[5] == 0);
    require(camera.image.has_value(), "capture contains no image");
    NativeLaserProfiler profiler(dll); profiler.initialize(kernel, net);
    profiler.set_extraction_config();
    NativeInterpolationConfig config;
    config.num_points = 1600; config.x_interval = 0.0625; config.x_start = -50; config.x_end = 49.9375;
    profiler.set_interpolation_config(config);
    const auto black = profiler.process(Image{64,64,Bytes(4096)});
    require(black.centerline.size() == 64 && black.points3d.size() == 64 && black.interpolated_3d.size() == 1600, "native black image preserves explicit sample counts");
    require(black.centerline[0].z == -999 && black.points3d[0].z == -999 && black.interpolated_3d[0].z == -999, "native v2_999 invalid sentinel");
    bool blocked = false;
    try { NativeLaserProfiler second(dll); second.initialize(kernel, net); }
    catch (const std::runtime_error&) { blocked = true; }
    require(blocked, "simultaneous native adapters cannot clobber DLL global state");
    const auto result = profiler.process(*camera.image);
    const auto original = profiler.process(*camera.image,30,false);
    require(original.centerline.size() == result.centerline.size() && original.points3d.size() == result.points3d.size(), "native ProcessToOriginal route");
    const auto comparison = compare_sf(result, camera);
    std::cout << std::setprecision(17) << "{\"center_count\":" << result.centerline.size() << ",\"center_crc32\":" << crc(result.centerline)
              << ",\"gray_crc32\":" << firmware_crc32(result.center_grays) << ",\"cloud_count\":" << result.points3d.size()
              << ",\"cloud_crc32\":" << crc(result.points3d) << ",\"interp_count\":" << result.interpolated_3d.size()
              << ",\"interp_crc32\":" << crc(result.interpolated_3d) << ",\"comparison\":{";
    const char* names[]{"center", "pointcloud", "interpolation"};
    const ErrorStats* stats[]{&comparison.center, &comparison.pointcloud, &comparison.interpolation};
    for (std::size_t i = 0; i < 3; ++i) {
        if (i) std::cout << ',';
        const auto& stat = *stats[i];
        std::cout << '"' << names[i] << "\":{\"status\":\"" << stat.status << "\",\"count\":" << stat.count
                  << ",\"mean_error\":" << stat.mean_error << ",\"rms_error\":" << stat.rms_error
                  << ",\"max_error\":" << stat.max_error << ",\"p95_error\":" << stat.p95_error
                  << ",\"signed_bias\":" << stat.signed_bias << ",\"dll_valid_count\":" << stat.dll_valid_count
                  << ",\"fpga_valid_count\":" << stat.fpga_valid_count << ",\"dll_only_valid_count\":" << stat.dll_only_valid_count
                  << ",\"fpga_only_valid_count\":" << stat.fpga_only_valid_count << ",\"both_invalid_count\":" << stat.both_invalid_count << '}';
    }
    std::cout << "}}\n";
}
void focus_probe(const std::string& path) {
    const auto frame = load_binary_file(path);
    require(frame.size() > 18, "capture is truncated");
    const auto camera = decode_camera_data(Bytes(frame.begin() + 14, frame.end() - 4), frame[5] == 0);
    require(camera.image.has_value(), "capture contains no image");
    FocusDetector detector;
    const auto first = detector.detect(*camera.image), second = detector.detect(*camera.image, Roi{307,202,951,213});
    std::cout << std::setprecision(17) << "{\"full_score\":" << first.score << ",\"full_mean\":" << first.mean_gray
              << ",\"full_saturation\":" << first.saturation << ",\"smoothed_roi_score\":" << second.score
              << ",\"roi_mean\":" << second.mean_gray << ",\"roi_saturation\":" << second.saturation << "}\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string(argv[1]) == "--native") { native_probe(argv[2],argv[3],argv[4],argv[5]); return 0; }
        if (argc == 3 && std::string(argv[1]) == "--focus") { focus_probe(argv[2]); return 0; }
        focus_test(); comparison_test();
        std::cout << "analysis tests passed: focus, masks, grids, counts, lag diagnostics\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
