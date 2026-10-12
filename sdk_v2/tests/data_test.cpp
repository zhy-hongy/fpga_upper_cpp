#include "fpga/data.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace fpga;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void invalid(const std::function<void()>& call) {
    try { call(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}
void put16(Bytes& data, std::uint16_t value) { data.push_back(value >> 8); data.push_back(value & 255); }
void put32(Bytes& data, std::uint32_t value) {
    data.push_back(value >> 24); data.push_back((value >> 16) & 255);
    data.push_back((value >> 8) & 255); data.push_back(value & 255);
}
void replace32(Bytes& data, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data.at(at + i) = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
void append(Bytes& data, const Bytes& other) { data.insert(data.end(), other.begin(), other.end()); }
void put_double(Bytes& data, double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i) data.push_back(static_cast<std::uint8_t>(bits >> (56 - i * 8)));
}
Bytes numeric_body(std::uint32_t count, std::uint16_t format, std::initializer_list<std::int32_t> values) {
    Bytes body; put32(body, count); put16(body, format);
    for (auto value : values) put32(body, static_cast<std::uint32_t>(value));
    return body;
}
Bytes image_body(std::uint16_t width, std::uint16_t height, const Bytes& pixels) {
    Bytes body; put16(body, width); put16(body, height); put16(body, 1); append(body, pixels);
    return body;
}
Bytes interleave(const Image& image) {
    const auto lanes = image.width / 128;
    Bytes raw(image.pixels.size());
    for (std::uint32_t pair = 0; pair < image.height / 2; ++pair)
        for (std::uint32_t cycle = 0; cycle < 128; ++cycle)
            for (std::uint32_t row = 0; row < 2; ++row)
                for (std::uint32_t lane = 0; lane < lanes; ++lane)
                    raw[((std::size_t(pair) * 128 + cycle) * 2 + row) * lanes + lanes - lane - 1]
                        = image.pixels[(std::size_t(pair) * 2 + row) * image.width + lane * 128 + cycle];
    return raw;
}

void image_tests() {
    Bytes raw(32); for (unsigned i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::uint8_t>(i);
    const Bytes expected{1,5,9,13,0,4,8,12,3,7,11,15,2,6,10,14,17,21,25,29,16,20,24,28,19,23,27,31,18,22,26,30};
    require(decode_fpga_image(raw, 8, 4, 2).pixels == expected, "lane and row-pair ordering");
    require(decode_standard_image(raw, 8, 4).pixels == raw, "row-major image passthrough");
    invalid([&] { decode_fpga_image(raw, 8, 4, 0); });
    invalid([&] { decode_fpga_image(raw, 8, 3, 2); });
    invalid([&] { decode_fpga_image(raw, 7, 4, 2); });
    invalid([&] { decode_fpga_image(Bytes(31), 8, 4, 2); });
    invalid([&] { decode_standard_image(raw, 0, 2); });
    invalid([&] { decode_standard_image(raw, 0xffffffffu, 0xffffffffu); });
    Bytes trailing = raw; trailing.push_back(99);
    require(decode_fpga_image(trailing, 8, 4, 2).pixels == expected, "image decoder ignores trailing data like Python");
    Image image{256, 4, Bytes(1024)};
    for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = static_cast<std::uint8_t>(i * 31 + i / 256);
    require(decode_legacy_image(interleave(image), 256, 4).image->pixels == image.pixels, "legacy raw image");
    Bytes prefixed{0, 0}; append(prefixed, interleave(image));
    require(decode_legacy_image(prefixed, 256, 4).image->pixels == image.pixels, "legacy result-prefixed image");
    invalid([&] { decode_legacy_image(Bytes(1025), 256, 4); });
}
void composite_tests() {
    Bytes data = pack_camera_data_block(1, image_body(128, 2, Bytes(256, 51)));
    append(data, pack_camera_data_block(2, numeric_body(3, 1, {81920, -180224, -999 * 65536})));
    append(data, pack_camera_data_block(3, numeric_body(2, 0, {-65536, 196608, 32768, -999 * 65536})));
    Bytes interpolation;
    put32(interpolation, std::uint32_t(-2 * 65536)); put32(interpolation, 65536); put32(interpolation, 65536);
    put32(interpolation, 3); put16(interpolation, 1);
    put32(interpolation, 0); put32(interpolation, 65536); put32(interpolation, std::uint32_t(-999 * 65536));
    append(data, pack_camera_data_block(4, interpolation));
    const auto result = decode_camera_data(data);
    require(result.block_length_includes_self && result.image->width == 128 && result.image->height == 2, "hardware Length includes self");
    require(result.image->pixels == Bytes(256, 51), "composite standard image");
    require(result.centerline.size() == 3 && result.centerline[0].x == 0 && result.centerline[0].z == 1.25 && result.centerline[1].z == -2.75, "Q16 centerline");
    require(result.centerline[2].z == -999, "invalid sentinel is preserved");
    require(result.points3d[0].x == -1 && result.points3d[0].y == 0 && result.points3d[0].z == 3, "format-zero hardware Q16 cloud");
    require(result.interpolated[0].x == -2 && result.interpolated[2].x == 0 && result.interpolated[2].z == -999, "inclusive/exclusive interpolation grid");
    require(result.interp_x_end == 1 && result.interp_interval == 1, "explicit grid metadata");
    require(looks_like_camera_data_blocks(data), "valid block recognition");
    for (std::size_t count = 0; count < 6; ++count) {
        const Bytes truncated(data.begin(), data.begin() + count);
        if (count != 2) invalid([&] { decode_camera_data(truncated); });
    }
    auto truncated = data; truncated.pop_back();
    require(!looks_like_camera_data_blocks(truncated), "truncated chain recognition");
    invalid([&] { decode_camera_data(truncated); });
    auto mixed = data;
    replace32(mixed, 2, 262); // First image block switches to legacy, rest stays hardware.
    invalid([&] { decode_camera_data(mixed); });
    const auto legacy = decode_camera_data(pack_camera_data_block(1, image_body(128, 2, Bytes(256, 51)), false));
    require(!legacy.block_length_includes_self, "legacy Length excludes self");
    auto corrected = data; corrected[8] = 4; corrected[9] = 128; // Declares 1152, actual two rows.
    const auto recovered = decode_camera_data(corrected);
    require(recovered.image_dimension_corrected && recovered.image_height_declared == 1152 && recovered.image->height == 2, "declared height corrected from complete block boundary");
    const auto skipped = decode_camera_data(pack_camera_data_block(2, numeric_body(3, 1, {65536})));
    require(skipped.centerline.empty() && !skipped.notes.empty(), "invalid numeric counts skipped with diagnostic");
    const auto floats = decode_camera_data(pack_camera_data_block(2, numeric_body(2, 2, {0x3fc00000, static_cast<std::int32_t>(0xc0200000u)})));
    require(floats.centerline[0].z == 1.5 && floats.centerline[1].z == -2.5, "big-endian float32 numeric values");
    Bytes int16_body; put32(int16_body, 1); put16(int16_body, 0); put16(int16_body, std::uint16_t(-3)); put16(int16_body, 7);
    const auto old_center = decode_camera_data(pack_camera_data_block(2, int16_body));
    require(old_center.centerline[0].x == -3 && old_center.centerline[0].z == 7, "legacy int16 center format");
    require(decode_camera_data({0, 7}).result_code == 7, "camera result-only response");
    require(!valid_numeric(-999) && !valid_numeric(-999.999) && !valid_numeric(std::numeric_limits<double>::quiet_NaN()) && valid_numeric(0), "numeric invalid values and true zero");
}
void layout_tests() {
    Image standard{1024, 256, Bytes(1024 * 256, 0)};
    for (std::size_t col = 0; col < standard.width; ++col) {
        const auto row = 40 + static_cast<std::size_t>(50 * (1 + std::sin(col * 0.004)));
        standard.pixels[row * standard.width + col] = 200;
    }
    const auto raw = interleave(standard);
    const auto payload = pack_camera_data_block(1, image_body(1024, 256, raw));
    require(decode_camera_data(payload, false).image->pixels == standard.pixels, "command raw declaration takes precedence over block Format");
    const auto declared = decode_camera_data(payload, true);
    require(declared.image->pixels == raw, "declared mode never silently changes uploaded declaration");
    const auto automatic = decode_camera_data(payload, true, ImageLayout::Auto);
    require(automatic.image_layout.auto_checked, "auto mode records both threshold checks");
    require(automatic.image_layout.selected == ImageLayout::FpgaRaw && automatic.image->pixels == standard.pixels, "strong continuity evidence corrects wrong declaration");
    require(decode_camera_data(payload, true, ImageLayout::FpgaRaw).image->pixels == standard.pixels, "manual raw override");
    const auto dark = decode_camera_data(pack_camera_data_block(1, image_body(1024, 256, Bytes(1024 * 256))), true, ImageLayout::Auto);
    require(dark.image_layout.selected == ImageLayout::Standard, "ambiguous auto layout keeps declaration");
}
void numeric_tests() {
    const auto center = decode_numeric_response(0x0302, {0xff, 0xff, 0xff, 0xfe});
    require(center.centerline[0].x == 65535 && center.centerline[0].z == -2, "independent centerline signed height");
    const auto contour = decode_numeric_response(0x0303, {0xff, 0xff, 0xff, 0xfe});
    require(contour.contour[0].x == 65535 && contour.contour[0].z == 65534, "independent unsigned contour");
    Bytes interp; put32(interp, 10); put32(interp, 14); put32(interp, 2); put32(interp, 6);
    put16(interp, std::uint16_t(-3)); put16(interp, 0); put16(interp, 5);
    const auto result = decode_numeric_response(0x0304, interp);
    require(result.interpolated.size() == 3 && result.interpolated[0].z == -3 && result.interpolated[2].x == 14, "independent interpolation int16 format");
    invalid([&] { decode_numeric_response(0x0302, {0, 0, 1}); });
    interp.push_back(0); invalid([&] { decode_numeric_response(0x0304, interp); });
    invalid([&] { decode_numeric_response(0x0300, {}); });
}
void batch_tests() {
    BatchData batch;
    batch.x_start_raw = 0xfffe0000u; batch.x_end_raw = 0x10000; batch.interval_raw = 32768; batch.point_count = 3;
    batch.records.push_back({0xdeadbeef, 0xabcde12345ull, 5, {{0x55, 0x66}}, {1.25, -999, -0.5}});
    batch.records.push_back({7, 2, 6, {{0, 0}}, {0, 3.5, -2}});
    const auto payload = pack_realtime_batch(batch), result_payload = pack_realtime_batch(decode_realtime_batch(payload));
    require(payload == result_payload, "batch roundtrip preserves per-record IDs timestamps padding and heights");
    const auto result = decode_realtime_batch(payload);
    require(result.records.size() == 2 && result.records[0].timestamp == 0xabcde12345ull && result.records[0].frame_no == 0xdeadbeef, "batch metadata is not inferred from first frame number");
    require(result.records[0].alignment_padding[1] == 0x66 && result.records[0].heights[0] == 1.25 && result.records[0].heights[1] == -999, "batch padding is excluded from heights");
    auto truncated = payload; truncated.pop_back(); invalid([&] { decode_realtime_batch(truncated); });
    auto appended = payload; appended.push_back(0); invalid([&] { decode_realtime_batch(appended); });
    invalid([&] { decode_realtime_batch(payload, 1); });
    invalid([&] { decode_realtime_batch(payload, 51); });
    invalid([&] { decode_realtime_batch(payload, 50, 2); });
    auto bad = payload; replace32(bad, 10, 0x80000000u); invalid([&] { decode_realtime_batch(bad); });
    bad = payload; replace32(bad, 14, 0); invalid([&] { decode_realtime_batch(bad); });
    require(decode_realtime_batch({0, 0}).records.empty() && decode_realtime_batch({0, 8}).result_code == 8, "result-only no-data or error batch");
    batch.records.clear(); require(decode_realtime_batch(pack_realtime_batch(batch)).records.empty(), "zero-count batch with valid common header");
    batch.records.resize(51);
    for (auto& record : batch.records) record.heights = {0, 1, 2};
    const auto all_payload = pack_all_batch(batch);
    require(decode_all_batch(all_payload).records.size() == 51, "all-batch supports counts exceeding realtime request cap");
    invalid([&] { decode_realtime_batch(all_payload); });
    invalid([&] { pack_realtime_batch(batch); });
    batch.records.clear();
    batch.records.push_back({1, 0x10000000000ull, 1, {{0,0}}, {0,0,0}}); invalid([&] { pack_realtime_batch(batch); });
}
void firmware_tests() {
    require(firmware_crc32({'1','2','3','4','5','6','7','8','9'}) == 0xcbf43926u, "standard CRC32 vector");
    require(firmware_crc32({}) == 0, "CRC32 empty data");
    const Bytes expected_request{0xaa,0x55,0x01,0x80,0x01,0x02,0x00,0x06,'U','P','D','A','T','E',0x02,0x4d,0xfa,0xfa};
    require(pack_upgrade_request(0x0102, 1) == expected_request, "upgrade checksum uses encoded bytes not word values");
    const auto decoded = decode_upgrade_frame(expected_request);
    require(decoded.sequence == 0x0102 && decoded.chip_id == 1 && decoded.payload == Bytes({'U','P','D','A','T','E'}) && decoded.down, "upgrade frame parse");
    require(decode_upgrade_frame(pack_upgrade_frame({4,0x83,8,{0xa5,0xa5},false})).down == false, "upgrade upstream response header");
    auto damaged = expected_request; damaged[8] ^= 1; invalid([&] { decode_upgrade_frame(damaged); });
    damaged = expected_request; damaged.push_back(0); invalid([&] { decode_upgrade_frame(damaged); });
    require(decode_upgrade_frame(pack_upgrade_control(1, 0x10203040, 1028, 4)).payload.size() == 8, "upgrade control payload");
    const auto empty = split_firmware_bytes({}); require(empty.chunks.size() == 1 && empty.chunks[0] == Bytes(4, 0), "empty firmware gets CRC-only chunk");
    const auto aligned = split_firmware_bytes(Bytes(1024, 7));
    require(aligned.chunks.size() == 1 && aligned.chunks.back().size() == 1028, "aligned final chunk may carry 1024 data plus CRC");
    require(decode_upgrade_frame(pack_upgrade_data(7, 9, aligned.chunks.back(), 3)).payload.size() == 1030, "upgrade codec accepts final extended chunk");
    invalid([&] { pack_upgrade_data(7, 9, Bytes(1029), 3); });
    const auto split = split_firmware_bytes(Bytes(1025, 8));
    require(split.chunks.size() == 2 && split.chunks[0].size() == 1024 && split.chunks[1].size() == 5, "short final chunk and CRC");
    const std::vector<FirmwareEntry> entries{{0, 0x10000, {1,2,3}}, {4, 0xff123456, {4,5}}};
    const auto pack = pack_firmware_entries(entries);
    const auto unpacked = parse_firmware_pack(pack);
    require(unpacked.size() == 2 && unpacked[0].data == entries[0].data && unpacked[1].chip_id == 4 && unpacked[1].flash_addr == 0xff123456, "firmware pack roundtrip");
    require(pack_firmware_entries(unpacked) == pack, "firmware pack deterministic encoding");
    damaged = pack; damaged.back() ^= 1; invalid([&] { parse_firmware_pack(damaged); });
    damaged = pack; replace32(damaged, 19, 10);
    Bytes headers(damaged.begin() + 10, damaged.begin() + 36); replace32(damaged, 6, firmware_crc32(headers));
    Bytes body(damaged.begin(), damaged.end() - 4); replace32(damaged, damaged.size() - 4, firmware_crc32(body));
    invalid([&] { parse_firmware_pack(damaged); }); // CRC-valid malicious reference into header.
    damaged = pack; replace32(damaged, 32, 37);
    headers = Bytes(damaged.begin() + 10, damaged.begin() + 36); replace32(damaged, 6, firmware_crc32(headers));
    body = Bytes(damaged.begin(), damaged.end() - 4); replace32(damaged, damaged.size() - 4, firmware_crc32(body));
    invalid([&] { parse_firmware_pack(damaged); }); // CRC-valid overlapping data blobs.
    invalid([&] { pack_firmware_entries({}); });
    invalid([&] { pack_firmware_entries(std::vector<FirmwareEntry>(17)); });
}

std::uint32_t point_crc(const std::vector<Point2>& points) {
    Bytes bytes;
    for (const auto& point : points) { put_double(bytes, point.x); put_double(bytes, point.z); }
    return firmware_crc32(bytes);
}
std::uint32_t point_crc(const std::vector<Point3>& points) {
    Bytes bytes;
    for (const auto& point : points) { put_double(bytes, point.x); put_double(bytes, point.y); put_double(bytes, point.z); }
    return firmware_crc32(bytes);
}
void probe(const std::string& path) {
    const auto frame = load_binary_file(path);
    require(frame.size() >= 18 && frame[0] == 0xaa && frame[1] == 0x55, "capture AA55 frame header");
    const auto command = (std::uint16_t(frame[4]) << 8) | frame[5];
    const auto result = decode_camera_data(Bytes(frame.begin() + 14, frame.end() - 4), command == 0x0300);
    std::cout << std::setprecision(17) << "{\"width\":" << (result.image ? result.image->width : 0)
              << ",\"height\":" << (result.image ? result.image->height : 0)
              << ",\"image_crc32\":" << (result.image ? firmware_crc32(result.image->pixels) : 0)
              << ",\"center_count\":" << result.centerline.size() << ",\"center_crc32\":" << point_crc(result.centerline)
              << ",\"cloud_count\":" << result.points3d.size() << ",\"cloud_crc32\":" << point_crc(result.points3d)
              << ",\"interp_count\":" << result.interpolated.size() << ",\"interp_crc32\":" << point_crc(result.interpolated)
              << ",\"x_start\":" << result.interp_x_start << ",\"x_end\":" << result.interp_x_end
              << ",\"interval\":" << result.interp_interval << ",\"height_corrected\":" << (result.image_dimension_corrected ? "true" : "false") << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--probe") { probe(argv[2]); return 0; }
        image_tests(); composite_tests(); layout_tests(); numeric_tests(); batch_tests(); firmware_tests();
        std::cout << "data tests passed: image, composite, layout, numeric, batch, firmware\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
