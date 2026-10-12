#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fpga {

using Bytes = std::vector<std::uint8_t>;

struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Bytes pixels; // Row-major Gray8, owned by the result.
};

struct Point2 { double x = 0.0; double z = 0.0; };
struct Point3 { double x = 0.0; double y = 0.0; double z = 0.0; };

enum class ImageLayout { Auto, Declared, FpgaRaw, Standard };

struct LayoutContinuity {
    std::uint32_t bright_columns = 0;
    std::uint32_t adjacent_pairs = 0;
    bool sufficient = false;
    double adjacent_p90_px = 0.0;
    double local_p90_px = 0.0;
};

struct ImageLayoutInfo {
    ImageLayout mode = ImageLayout::Declared;
    ImageLayout declared = ImageLayout::Standard;
    ImageLayout selected = ImageLayout::Standard;
    std::array<LayoutContinuity, 2> declared_checks{}; // Gray thresholds 30 / 40.
    std::array<LayoutContinuity, 2> alternative_checks{};
    bool auto_checked = false;
};

struct CameraData {
    std::uint16_t result_code = 0;
    bool rearranged = true;
    bool block_length_includes_self = true;
    std::optional<Image> image;
    std::uint16_t image_format = 0;
    std::uint16_t image_height_declared = 0;
    bool image_dimension_corrected = false;
    ImageLayoutInfo image_layout;
    std::vector<Point2> centerline;
    std::vector<Point2> contour; // Independent 0x0303 legacy unsigned X/Y contour.
    std::vector<Point3> points3d; // Composite block 0x0003 sends X/Z; Y is 0.
    std::vector<Point2> interpolated;
    double interp_x_start = 0.0;
    double interp_x_end = 0.0;
    double interp_interval = 0.0;
    std::uint32_t interp_count = 0;
    std::uint32_t interp_x_start_raw = 0;
    std::uint32_t interp_x_end_raw = 0;
    std::uint32_t interp_interval_raw = 0;
    std::vector<std::string> notes;
};

// Invalid data throws std::invalid_argument. Composite images require exact length;
// standalone image decoders intentionally ignore extra trailing bytes like Python.
Image decode_fpga_image(const Bytes& data, std::uint32_t width = 2048,
                        std::uint32_t height = 1152, std::uint32_t lanes = 16);
Image decode_standard_image(const Bytes& data, std::uint32_t width, std::uint32_t height);
CameraData decode_camera_data(const Bytes& data, bool rearranged = true,
                              ImageLayout mode = ImageLayout::Declared);
bool looks_like_camera_data_blocks(const Bytes& data) noexcept;
CameraData decode_legacy_image(const Bytes& data, std::uint32_t width = 2048,
                               std::uint32_t height = 1152);
CameraData decode_numeric_response(std::uint16_t command, const Bytes& data);
Bytes pack_camera_data_block(std::uint16_t type, const Bytes& body,
                             bool hardware_length = true);
bool valid_numeric(double value) noexcept;

struct BatchRecord {
    std::uint32_t frame_no = 0;
    std::uint64_t timestamp = 0; // Raw unsigned 40-bit counter; unit is unspecified.
    std::uint8_t batch_index = 0;
    std::array<std::uint8_t, 2> alignment_padding{{0, 0}};
    std::vector<double> heights; // Signed Q16.16 converted to doubles.
};

struct BatchData {
    std::uint16_t result_code = 0;
    std::uint32_t x_start_raw = 0;
    std::uint32_t x_end_raw = 0;
    std::uint32_t interval_raw = 0;
    std::uint32_t point_count = 0;
    std::vector<BatchRecord> records;
};

BatchData decode_realtime_batch(const Bytes& data, std::uint16_t max_count = 50,
                               std::uint32_t max_points = 65536);
Bytes pack_realtime_batch(const BatchData& batch);
// 0x0205 shares the record layout but its count can exceed the 0x0204 limit of 50.
BatchData decode_all_batch(const Bytes& data, std::uint32_t max_points = 65536);
Bytes pack_all_batch(const BatchData& batch);

struct UpgradeFrame {
    std::uint8_t chip_id = 0;
    std::uint8_t function = 0x80;
    std::uint16_t sequence = 0;
    Bytes payload;
    bool down = true;
};

Bytes pack_upgrade_frame(const UpgradeFrame& frame);
UpgradeFrame decode_upgrade_frame(const Bytes& data);
Bytes pack_upgrade_request(std::uint16_t sequence, std::uint8_t chip_id = 0);
Bytes pack_upgrade_control(std::uint16_t sequence, std::uint32_t flash_addr,
                           std::uint32_t file_size, std::uint8_t chip_id = 0);
Bytes pack_upgrade_data(std::uint16_t sequence, std::uint16_t frame_index,
                        const Bytes& chunk, std::uint8_t chip_id = 0);
Bytes pack_upgrade_result_ack(std::uint16_t sequence, std::uint8_t chip_id = 0);

struct FirmwareChunks {
    std::vector<Bytes> chunks;
    std::uint32_t crc32 = 0;
};

struct FirmwareEntry {
    std::uint8_t chip_id = 0;
    std::uint32_t flash_addr = 0;
    Bytes data;
};

std::uint32_t firmware_crc32(const Bytes& data);
FirmwareChunks split_firmware_bytes(const Bytes& data);
Bytes pack_firmware_entries(const std::vector<FirmwareEntry>& entries);
std::vector<FirmwareEntry> parse_firmware_pack(const Bytes& data);
std::vector<FirmwareEntry> load_firmware_pack(const std::string& path);
void save_firmware_pack(const std::string& path, const std::vector<FirmwareEntry>& entries);
Bytes load_binary_file(const std::string& path);
void save_binary_file(const std::string& path, const Bytes& data);

} // namespace fpga
