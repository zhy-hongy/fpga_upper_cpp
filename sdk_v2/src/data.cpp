#include "fpga/data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fpga {
namespace {

[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument(message); }

std::uint16_t u16(const Bytes& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2) fail("truncated uint16");
    return static_cast<std::uint16_t>((std::uint16_t(data[offset]) << 8) | data[offset + 1]);
}
std::uint32_t u32(const Bytes& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) fail("truncated uint32");
    return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16)
         | (std::uint32_t(data[offset + 2]) << 8) | data[offset + 3];
}
std::int32_t s32(const Bytes& data, std::size_t offset) {
    const auto raw = u32(data, offset);
    return raw <= 0x7fffffffu ? static_cast<std::int32_t>(raw)
         : static_cast<std::int32_t>(static_cast<std::int64_t>(raw) - 0x100000000ll);
}
std::int16_t s16(const Bytes& data, std::size_t offset) {
    const auto raw = u16(data, offset);
    return raw <= 0x7fffu ? static_cast<std::int16_t>(raw)
         : static_cast<std::int16_t>(static_cast<std::int32_t>(raw) - 0x10000);
}
void put16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void put32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void set32(Bytes& out, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.at(offset + i) = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}
std::size_t image_size(std::uint32_t width, std::uint32_t height) {
    if (!width || !height) fail("image dimensions must be positive");
    const std::uint64_t size = std::uint64_t(width) * height;
    if (size > std::numeric_limits<std::size_t>::max()) fail("image dimensions overflow");
    return static_cast<std::size_t>(size);
}
Image decode_image(const Bytes& data, std::size_t offset, std::uint32_t width,
                   std::uint32_t height, bool raw, std::uint32_t lanes) {
    const auto pixels = image_size(width, height);
    if (offset > data.size() || pixels > data.size() - offset) fail("image data is truncated");
    if (raw && (!lanes || width % lanes || height % 2))
        fail("FPGA image needs even height and width divisible by positive lane count");
    Image result{width, height, Bytes(pixels)};
    if (!raw) {
        std::copy_n(data.begin() + offset, pixels, result.pixels.begin());
        return result;
    }
    const auto cycles = width / lanes;
    for (std::uint32_t pair = 0; pair < height / 2; ++pair)
        for (std::uint32_t cycle = 0; cycle < cycles; ++cycle)
            for (std::uint32_t row = 0; row < 2; ++row)
                for (std::uint32_t lane = 0; lane < lanes; ++lane) {
                    const auto input = offset + ((std::size_t(pair) * cycles + cycle) * 2 + row) * lanes + (lanes - lane - 1);
                    const auto output = (std::size_t(pair) * 2 + row) * width + std::size_t(lane) * cycles + cycle;
                    result.pixels[output] = data[input];
                }
    return result;
}

struct Span { std::uint16_t type; std::size_t start; std::size_t size; };
std::vector<Span> spans(const Bytes& data, bool includes_self) {
    std::vector<Span> result;
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (data.size() - offset < 6) fail("incomplete camera block header");
        const auto type = u16(data, offset);
        const auto declared = u32(data, offset + 2);
        if (type < 1 || type > 4) fail("unknown camera block type");
        if (includes_self && declared < 4) fail("camera block length is below self length");
        const std::size_t size = declared - (includes_self ? 4 : 0);
        const std::size_t minimum = type == 4 ? 18 : 6;
        if (size < minimum) fail("camera block parameter header is truncated");
        const std::size_t start = offset + 6;
        if (size > data.size() - start) fail("camera block body is truncated");
        if (type == 1) {
            const auto width = u16(data, start), height = u16(data, start + 2);
            const auto pixels = size - 6;
            if (!width || !height) fail("camera image dimensions must be positive");
            if (pixels != std::size_t(width) * height &&
                (pixels % width || pixels / width == 0 || pixels / width > 65535))
                fail("camera image length does not prove declared or corrected dimensions");
        }
        result.push_back(Span{type, start, size});
        offset = start + size;
    }
    if (result.empty()) fail("camera data has no blocks");
    return result;
}
std::pair<std::vector<Span>, bool> camera_layout(const Bytes& data) {
    try { return {spans(data, true), true}; }
    catch (const std::invalid_argument&) {}
    return {spans(data, false), false};
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double at = (values.size() - 1) * fraction;
    const auto lo = static_cast<std::size_t>(std::floor(at));
    const auto hi = static_cast<std::size_t>(std::ceil(at));
    return values[lo] + (values[hi] - values[lo]) * (at - lo);
}
struct PeakProfile { std::vector<double> peaks; Bytes gray; };
PeakProfile peaks(const Image& image) {
    PeakProfile result{std::vector<double>(image.width, 0.0), Bytes(image.width, 0)};
    for (std::uint32_t row = 0; row < image.height; ++row)
        for (std::uint32_t col = 0; col < image.width; ++col) {
            const auto pixel = image.pixels[std::size_t(row) * image.width + col];
            if (pixel > result.gray[col]) { result.gray[col] = pixel; result.peaks[col] = row; }
        }
    return result;
}
LayoutContinuity continuity(const Image& image, const PeakProfile& profile, std::uint8_t threshold) {
    LayoutContinuity result;
    std::vector<double> adjacent, local;
    for (std::size_t col = 0; col < image.width; ++col) {
        if (profile.gray[col] < threshold) continue;
        ++result.bright_columns;
        if (col && profile.gray[col - 1] >= threshold)
            adjacent.push_back(std::abs(profile.peaks[col] - profile.peaks[col - 1]));
        std::vector<double> neighbours;
        for (std::size_t other = col > 8 ? col - 8 : 0; other <= std::min<std::size_t>(col + 8, image.width - 1); ++other)
            if (other != col && profile.gray[other] >= threshold) neighbours.push_back(profile.peaks[other]);
        if (neighbours.size() >= 4) local.push_back(std::abs(profile.peaks[col] - percentile(std::move(neighbours), 0.5)));
    }
    result.adjacent_pairs = static_cast<std::uint32_t>(adjacent.size());
    const auto minimum = std::max<std::uint32_t>(128, image.width / 2);
    result.sufficient = image.width >= 256 && image.height >= 16 && result.bright_columns >= minimum
                     && adjacent.size() >= minimum && local.size() >= minimum;
    result.adjacent_p90_px = percentile(std::move(adjacent), 0.90);
    result.local_p90_px = percentile(std::move(local), 0.90);
    return result;
}
bool strongly_better(const LayoutContinuity& winner, const LayoutContinuity& loser) {
    auto better = [](double w, double l) { return w <= 5 && l >= 20 && l >= 4 * std::max(w, 1.0); };
    return winner.sufficient && loser.sufficient && better(winner.adjacent_p90_px, loser.adjacent_p90_px)
           && better(winner.local_p90_px, loser.local_p90_px);
}
Image comparison_image(const Bytes& data, const Span& span, std::uint32_t width,
                       std::uint32_t height, bool rearranged, ImageLayout mode, ImageLayoutInfo& info) {
    info.mode = mode;
    info.declared = rearranged ? ImageLayout::Standard : ImageLayout::FpgaRaw;
    info.selected = mode == ImageLayout::Auto || mode == ImageLayout::Declared ? info.declared : mode;
    auto decode = [&](ImageLayout layout) {
        if (layout == ImageLayout::FpgaRaw && (width < 128 || width % 128))
            fail("FPGA camera image width must be a multiple of 128");
        return decode_image(data, span.start + 6, width, height, layout == ImageLayout::FpgaRaw, width / 128);
    };
    Image selected = decode(info.selected);
    if (mode != ImageLayout::Auto || width < 256 || width % 128 || height % 2 || height < 16) return selected;
    const auto other = info.declared == ImageLayout::Standard ? ImageLayout::FpgaRaw : ImageLayout::Standard;
    Image alternative = decode(other);
    const auto declared_profile = peaks(selected), other_profile = peaks(alternative);
    info.auto_checked = true;
    bool change = true;
    for (std::size_t i = 0; i < 2; ++i) {
        const std::uint8_t threshold = i == 0 ? 30 : 40;
        info.declared_checks[i] = continuity(selected, declared_profile, threshold);
        info.alternative_checks[i] = continuity(alternative, other_profile, threshold);
        change = change && strongly_better(info.alternative_checks[i], info.declared_checks[i]);
    }
    if (change) { info.selected = other; return alternative; }
    return selected;
}

double numeric_value(const Bytes& data, std::size_t offset, std::uint16_t format) {
    if (format != 2) return s32(data, offset) / 65536.0;
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE754 float32 required");
    const auto bits = u32(data, offset);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void diagnose(CameraData& result) {
    std::vector<Point2> cloud, grid;
    for (const auto& point : result.points3d)
        if (valid_numeric(point.x) && valid_numeric(point.z)) cloud.push_back({point.x, point.z});
    for (const auto& point : result.interpolated)
        if (valid_numeric(point.x) && valid_numeric(point.z)) grid.push_back(point);
    if (cloud.size() >= 2 && std::all_of(cloud.begin(), cloud.end(), [&](const Point2& p) {
        return p.x == cloud.front().x && p.z == cloud.front().z;
    })) result.notes.push_back("FLAT_POINTCLOUD: all valid uploaded X/Z values are identical; uploaded values preserved");
    if (grid.size() >= 2 && std::all_of(grid.begin(), grid.end(), [&](const Point2& p) {
        return p.z == grid.front().z;
    })) result.notes.push_back("FLAT_INTERPOLATION: all valid uploaded Z values are identical; uploaded values preserved");
    if (cloud.size() < 2 || grid.size() < 2) return;
    auto range = [](const std::vector<Point2>& points) {
        auto extrema = std::minmax_element(points.begin(), points.end(), [](const Point2& a, const Point2& b) { return a.x < b.x; });
        return extrema.second->x - extrema.first->x;
    };
    if (range(grid) > 4 * std::max({range(cloud), std::abs(result.interp_interval), 1.0 / 65536.0}))
        result.notes.push_back("INTERP_GRID_OUTSIDE_CLOUD: uploaded interpolation X range greatly exceeds cloud X range; values preserved");
}

std::uint16_t checksum(const Bytes& data, std::size_t begin, std::size_t end) {
    std::uint32_t sum = 0;
    for (auto i = begin; i < end; ++i) sum += data[i];
    return static_cast<std::uint16_t>(sum);
}
std::uint32_t crc32_range(const Bytes& data, std::size_t begin, std::size_t end) {
    std::uint32_t crc = 0xffffffffu;
    for (auto i = begin; i < end; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}
std::int32_t q16(double value) {
    const double scaled = value * 65536.0;
    if (!std::isfinite(scaled) || scaled < std::numeric_limits<std::int32_t>::min() || scaled > std::numeric_limits<std::int32_t>::max())
        fail("height cannot be represented as signed Q16.16");
    return static_cast<std::int32_t>(std::round(scaled));
}

} // namespace

Image decode_fpga_image(const Bytes& data, std::uint32_t width, std::uint32_t height, std::uint32_t lanes) {
    return decode_image(data, 0, width, height, true, lanes);
}
Image decode_standard_image(const Bytes& data, std::uint32_t width, std::uint32_t height) {
    return decode_image(data, 0, width, height, false, 0);
}
bool valid_numeric(double value) noexcept {
    return std::isfinite(value) && value != -999.0 && std::abs(value + 999.999) > 0.0001;
}
bool looks_like_camera_data_blocks(const Bytes& data) noexcept {
    try { camera_layout(data); return true; } catch (...) { return false; }
}
CameraData decode_camera_data(const Bytes& data, bool rearranged, ImageLayout mode) {
    CameraData result;
    result.rearranged = rearranged;
    if (data.size() == 2) { result.result_code = u16(data, 0); return result; }
    const auto layout = camera_layout(data);
    result.block_length_includes_self = layout.second;
    for (const auto& span : layout.first) {
        const auto start = span.start;
        if (span.type == 1) {
            const auto width = u16(data, start), height = u16(data, start + 2);
            result.image_format = u16(data, start + 4);
            result.image_height_declared = height;
            const auto actual_height = static_cast<std::uint32_t>((span.size - 6) / width);
            result.image_dimension_corrected = actual_height != height;
            if (result.image_dimension_corrected) result.notes.push_back("IMAGE_HEIGHT_MISMATCH: height corrected from complete block pixel length");
            result.image = comparison_image(data, span, width, actual_height, rearranged, mode, result.image_layout);
            if (result.image_layout.selected != result.image_layout.declared)
                result.notes.push_back("IMAGE_LAYOUT_MISMATCH: selected image layout differs from command declaration");
            continue;
        }
        const auto format = u16(data, start + (span.type == 4 ? 16 : 4));
        const auto count = u32(data, start + (span.type == 4 ? 12 : 0));
        const auto header_size = span.type == 4 ? 18u : 6u;
        const auto stride = span.type == 3 ? 8u : 4u;
        if (format > 2 || std::uint64_t(count) * stride != span.size - header_size) {
            result.notes.push_back("COUNT_MISMATCH: numeric block has unsupported format or wrong point count; skipped");
            continue;
        }
        if (span.type == 2) {
            result.centerline.clear(); result.centerline.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto at = start + 6 + std::size_t(i) * 4;
                result.centerline.push_back(format == 0 ? Point2{double(s16(data, at)), double(s16(data, at + 2))}
                    : Point2{double(i), numeric_value(data, at, format)});
            }
        } else if (span.type == 3) {
            result.points3d.clear(); result.points3d.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto at = start + 6 + std::size_t(i) * 8;
                result.points3d.push_back({numeric_value(data, at, format), 0, numeric_value(data, at + 4, format)});
            }
        } else {
            result.interpolated.clear(); result.interpolated.reserve(count);
            result.interp_x_start_raw = u32(data, start);
            result.interp_x_end_raw = u32(data, start + 4);
            result.interp_interval_raw = u32(data, start + 8);
            const double divisor = format == 2 ? 1.0 : 65536.0;
            result.interp_x_start = s32(data, start) / divisor;
            result.interp_x_end = s32(data, start + 4) / divisor;
            result.interp_interval = s32(data, start + 8) / divisor;
            result.interp_count = count;
            if (!result.interp_interval && count > 1)
                result.interp_interval = (result.interp_x_end - result.interp_x_start) / (count - 1);
            for (std::uint32_t i = 0; i < count; ++i)
                result.interpolated.push_back({result.interp_x_start + i * result.interp_interval,
                    numeric_value(data, start + 18 + std::size_t(i) * 4, format)});
            if (count > 1 && result.interp_interval) {
                const auto inclusive = result.interp_x_start + (count - 1) * result.interp_interval;
                const auto exclusive = result.interp_x_start + count * result.interp_interval;
                const auto tolerance = std::max({count * 0.5 / 65536.0, std::abs(result.interp_interval) * 0.01, 1.0 / 65536.0});
                if (std::min(std::abs(inclusive - result.interp_x_end), std::abs(exclusive - result.interp_x_end)) > tolerance)
                    result.notes.push_back("INTERP_GRID_MISMATCH: interval/count do not match inclusive or exclusive endpoint; uploaded grid preserved");
            }
        }
    }
    diagnose(result);
    return result;
}
CameraData decode_legacy_image(const Bytes& data, std::uint32_t width, std::uint32_t height) {
    const auto size = image_size(width, height);
    if (data.size() != size && data.size() != size + 2) fail("legacy image must exactly match configured pixel count, with optional 2-byte prefix");
    if (looks_like_camera_data_blocks(data)) fail("block response must be decoded as composite camera data");
    CameraData result;
    result.rearranged = false;
    result.image_format = 2;
    result.image_layout.declared = result.image_layout.selected = ImageLayout::FpgaRaw;
    if (width < 128 || width % 128) fail("legacy FPGA image width must be a multiple of 128");
    result.image = decode_image(data, data.size() - size, width, height, true, width / 128);
    return result;
}
CameraData decode_numeric_response(std::uint16_t command, const Bytes& data) {
    CameraData result;
    if (command < 0x0302 || command > 0x0304) fail("unsupported independent camera numeric command");
    if (data.size() == 2) { result.result_code = u16(data, 0); return result; }
    if (command != 0x0304) {
        if (data.size() % 4) fail("numeric point data must contain complete 4-byte points");
        for (std::size_t at = 0; at < data.size(); at += 4) {
            if (command == 0x0302) result.centerline.push_back({double(u16(data, at)), double(s16(data, at + 2))});
            else result.contour.push_back({double(u16(data, at)), double(u16(data, at + 2))});
        }
        return result;
    }
    if (data.size() < 16) fail("independent interpolation header is truncated");
    const auto length = u32(data, 12);
    if (length != data.size() - 16 || length % 2) fail("independent interpolation payload length is inconsistent");
    result.interp_x_start_raw = u32(data, 0);
    result.interp_x_end_raw = u32(data, 4);
    result.interp_interval_raw = u32(data, 8);
    result.interp_x_start = result.interp_x_start_raw;
    result.interp_x_end = result.interp_x_end_raw;
    result.interp_interval = result.interp_interval_raw;
    result.interp_count = length / 2;
    for (std::uint32_t i = 0; i < result.interp_count; ++i)
        result.interpolated.push_back({result.interp_x_start + i * result.interp_interval, double(s16(data, 16 + std::size_t(i) * 2))});
    return result;
}
Bytes pack_camera_data_block(std::uint16_t type, const Bytes& body, bool hardware_length) {
    if (type < 1 || type > 4) fail("unknown camera block type");
    if (body.size() > std::numeric_limits<std::uint32_t>::max() - (hardware_length ? 4 : 0)) fail("camera block exceeds uint32 length");
    Bytes result; result.reserve(6 + body.size());
    put16(result, type); put32(result, static_cast<std::uint32_t>(body.size()) + (hardware_length ? 4 : 0));
    result.insert(result.end(), body.begin(), body.end());
    return result;
}

BatchData decode_realtime_batch(const Bytes& data, std::uint16_t max_count, std::uint32_t max_points) {
    if (!max_count || max_count > 50) fail("invalid realtime batch count limit");
    auto result = decode_all_batch(data, max_points);
    if (result.records.size() > max_count) fail("batch count exceeds request limit");
    return result;
}
BatchData decode_all_batch(const Bytes& data, std::uint32_t max_points) {
    if (!max_points) fail("invalid batch point limit");
    BatchData result;
    if (data.size() == 2) { result.result_code = u16(data, 0); return result; }
    if (data.size() < 18) fail("batch common header is truncated");
    const auto count = u16(data, 0);
    result.x_start_raw = u32(data, 2);
    result.x_end_raw = u32(data, 6);
    result.interval_raw = u32(data, 10);
    result.point_count = u32(data, 14);
    if (!result.point_count || result.point_count > max_points) fail("batch interpolation point count is invalid");
    if (!result.interval_raw || result.interval_raw >= 0x80000000u) fail("batch interval must be positive signed 32-bit");
    const std::uint64_t stride = 12 + std::uint64_t(result.point_count) * 4;
    const std::uint64_t expected = 18 + std::uint64_t(count) * stride;
    if (expected != data.size()) fail("batch payload length is inconsistent");
    result.records.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>(18 + i * stride);
        BatchRecord record;
        record.frame_no = u32(data, at);
        for (unsigned j = 0; j < 5; ++j) record.timestamp = (record.timestamp << 8) | data[at + 4 + j];
        record.batch_index = data[at + 9];
        record.alignment_padding = {{data[at + 10], data[at + 11]}};
        record.heights.reserve(result.point_count);
        for (std::uint32_t j = 0; j < result.point_count; ++j)
            record.heights.push_back(s32(data, at + 12 + std::size_t(j) * 4) / 65536.0);
        result.records.push_back(std::move(record));
    }
    return result;
}
Bytes pack_realtime_batch(const BatchData& batch) {
    if (batch.records.size() > 50) fail("realtime batch count exceeds 50");
    return pack_all_batch(batch);
}
Bytes pack_all_batch(const BatchData& batch) {
    Bytes result;
    if (batch.result_code) {
        if (!batch.records.empty()) fail("error batch cannot contain data records");
        put16(result, batch.result_code); return result;
    }
    if (batch.records.size() > 65535 || !batch.point_count || batch.point_count > 65536 ||
        !batch.interval_raw || batch.interval_raw >= 0x80000000u) fail("invalid batch parameters");
    put16(result, static_cast<std::uint16_t>(batch.records.size()));
    put32(result, batch.x_start_raw); put32(result, batch.x_end_raw); put32(result, batch.interval_raw); put32(result, batch.point_count);
    for (const auto& record : batch.records) {
        if (record.timestamp > 0xffffffffffull || record.heights.size() != batch.point_count) fail("invalid batch record timestamp or point count");
        put32(result, record.frame_no);
        for (unsigned i = 0; i < 5; ++i) result.push_back(static_cast<std::uint8_t>(record.timestamp >> (32 - i * 8)));
        result.push_back(record.batch_index);
        result.insert(result.end(), record.alignment_padding.begin(), record.alignment_padding.end());
        for (const auto height : record.heights) put32(result, static_cast<std::uint32_t>(q16(height)));
    }
    return result;
}

Bytes pack_upgrade_frame(const UpgradeFrame& frame) {
    if (frame.payload.size() > 65535) fail("upgrade payload exceeds uint16 length");
    Bytes result; result.reserve(frame.payload.size() + 12);
    put16(result, frame.down ? 0xaa55 : 0x55aa);
    result.push_back(frame.chip_id); result.push_back(frame.function);
    put16(result, frame.sequence); put16(result, static_cast<std::uint16_t>(frame.payload.size()));
    result.insert(result.end(), frame.payload.begin(), frame.payload.end());
    put16(result, checksum(result, 2, result.size())); put16(result, 0xfafa);
    return result;
}
UpgradeFrame decode_upgrade_frame(const Bytes& data) {
    if (data.size() < 12) fail("upgrade frame is truncated");
    const auto head = u16(data, 0);
    if (head != 0xaa55 && head != 0x55aa) fail("invalid upgrade frame header");
    const auto length = u16(data, 6);
    if (data.size() != std::size_t(length) + 12 || u16(data, data.size() - 2) != 0xfafa) fail("invalid upgrade length or tail");
    if (u16(data, data.size() - 4) != checksum(data, 2, data.size() - 4)) fail("upgrade checksum mismatch");
    return UpgradeFrame{data[2], data[3], u16(data, 4), Bytes(data.begin() + 8, data.end() - 4), head == 0xaa55};
}
Bytes pack_upgrade_request(std::uint16_t sequence, std::uint8_t chip_id) {
    return pack_upgrade_frame({chip_id, 0x80, sequence, {'U', 'P', 'D', 'A', 'T', 'E'}, true});
}
Bytes pack_upgrade_control(std::uint16_t sequence, std::uint32_t flash_addr, std::uint32_t file_size, std::uint8_t chip_id) {
    Bytes payload; put32(payload, flash_addr); put32(payload, file_size);
    return pack_upgrade_frame({chip_id, 0x81, sequence, std::move(payload), true});
}
Bytes pack_upgrade_data(std::uint16_t sequence, std::uint16_t frame_index, const Bytes& chunk, std::uint8_t chip_id) {
    // Python appends CRC32 to the last 1024-byte chunk, so that final chunk may be 1028 bytes.
    if (chunk.size() > 1028) fail("firmware chunk exceeds 1024 bytes plus final CRC32");
    Bytes payload; put16(payload, frame_index); payload.insert(payload.end(), chunk.begin(), chunk.end());
    return pack_upgrade_frame({chip_id, 0x82, sequence, std::move(payload), true});
}
Bytes pack_upgrade_result_ack(std::uint16_t sequence, std::uint8_t chip_id) {
    return pack_upgrade_frame({chip_id, 0x83, sequence, {0xa5, 0xa5}, true});
}
std::uint32_t firmware_crc32(const Bytes& data) { return crc32_range(data, 0, data.size()); }
FirmwareChunks split_firmware_bytes(const Bytes& data) {
    FirmwareChunks result;
    result.crc32 = firmware_crc32(data);
    for (std::size_t start = 0; start < data.size();) {
        const auto size = std::min<std::size_t>(1024, data.size() - start);
        result.chunks.emplace_back(data.begin() + start, data.begin() + start + size);
        start += size;
    }
    if (result.chunks.empty()) result.chunks.emplace_back();
    put32(result.chunks.back(), result.crc32);
    return result;
}
Bytes pack_firmware_entries(const std::vector<FirmwareEntry>& entries) {
    if (entries.empty() || entries.size() > 16) fail("firmware pack must contain 1..16 entries");
    std::uint64_t total = 10 + entries.size() * 13;
    for (const auto& entry : entries) total += entry.data.size();
    if (total + 4 > std::numeric_limits<std::uint32_t>::max()) fail("firmware pack exceeds uint32 offsets");
    Bytes result{'F', 'P', 'A', 'C', 1, static_cast<std::uint8_t>(entries.size()), 0, 0, 0, 0};
    result.reserve(static_cast<std::size_t>(total + 4));
    std::uint32_t offset = static_cast<std::uint32_t>(10 + entries.size() * 13);
    for (const auto& entry : entries) {
        result.push_back(entry.chip_id); put32(result, entry.flash_addr);
        put32(result, static_cast<std::uint32_t>(entry.data.size())); put32(result, offset);
        offset += static_cast<std::uint32_t>(entry.data.size());
    }
    set32(result, 6, crc32_range(result, 10, result.size()));
    for (const auto& entry : entries) result.insert(result.end(), entry.data.begin(), entry.data.end());
    put32(result, firmware_crc32(result));
    return result;
}
std::vector<FirmwareEntry> parse_firmware_pack(const Bytes& data) {
    if (data.size() < 14) fail("firmware pack header is truncated");
    if (!std::equal(data.begin(), data.begin() + 4, "FPAC")) fail("firmware pack magic mismatch");
    if (data[4] != 1) fail("unsupported firmware pack version");
    const auto count = data[5];
    if (!count || count > 16) fail("firmware pack entry count is invalid");
    const auto data_start = std::size_t(10 + 13 * count);
    if (data.size() < data_start + 4) fail("firmware pack entries are truncated");
    if (u32(data, data.size() - 4) != crc32_range(data, 0, data.size() - 4)) fail("firmware pack file CRC32 mismatch");
    if (u32(data, 6) != crc32_range(data, 10, data_start)) fail("firmware pack header CRC32 mismatch");
    std::vector<FirmwareEntry> result;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    result.reserve(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        const auto at = std::size_t(10 + 13 * i);
        const auto size = u32(data, at + 5), offset = u32(data, at + 9);
        if (offset < data_start || offset > data.size() - 4 || size > data.size() - 4 - offset)
            fail("firmware pack data range is out of bounds or overlaps header");
        if (size) ranges.emplace_back(offset, std::size_t(offset) + size);
        result.push_back({data[at], u32(data, at + 1), Bytes(data.begin() + offset, data.begin() + offset + size)});
    }
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i].first < ranges[i - 1].second) fail("firmware pack data entries overlap");
    return result;
}
Bytes load_binary_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open binary file: " + path);
    const auto length = stream.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("cannot determine binary file size: " + path);
    Bytes result(static_cast<std::size_t>(length));
    stream.seekg(0);
    if (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("cannot read binary file: " + path);
    return result;
}
void save_binary_file(const std::string& path, const Bytes& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || (!data.empty() && !stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))))
        throw std::runtime_error("cannot write binary file: " + path);
    stream.close();
    if (!stream) throw std::runtime_error("cannot close binary file: " + path);
}
std::vector<FirmwareEntry> load_firmware_pack(const std::string& path) { return parse_firmware_pack(load_binary_file(path)); }
void save_firmware_pack(const std::string& path, const std::vector<FirmwareEntry>& entries) { save_binary_file(path, pack_firmware_entries(entries)); }

} // namespace fpga
