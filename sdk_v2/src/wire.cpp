#include "fpga/wire.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace fpga {
namespace {

void append_u16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void append_u32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
void require_size(const Bytes& bytes, std::size_t size, const char* label) {
    if (bytes.size() != size)
        throw ProtocolError(std::string(label) + ": expected " +
                            std::to_string(size) + " bytes, received " +
                            std::to_string(bytes.size()));
}
void require_range(const Bytes& data, std::size_t offset, std::size_t length) {
    if (offset > data.size() || length > data.size() - offset)
        throw ProtocolError("truncated integer field");
}
void validate_packet_numbers(const TransportPacket& packet) {
    if (packet.retransmit == 0 &&
        (packet.total == 0 || packet.current == 0 || packet.current > packet.total))
        throw ProtocolError("invalid transport packet number");
}
double round_even(double value) {
    const double lower = std::floor(value);
    const double part = value - lower;
    if (part < 0.5) return lower;
    if (part > 0.5) return lower + 1.0;
    return std::fmod(lower, 2.0) == 0.0 ? lower : lower + 1.0;
}
Bytes ip_bytes(const std::string& address) {
    Bytes result;
    std::size_t begin = 0;
    for (int part = 0; part != 4; ++part) {
        const auto end = address.find('.', begin);
        if ((part != 3 && end == std::string::npos) ||
            (part == 3 && end != std::string::npos))
            throw ProtocolError("invalid IPv4 address: " + address);
        const auto token = address.substr(begin, end == std::string::npos ? end : end - begin);
        if (token.empty() || token.size() > 3 ||
            token.find_first_not_of("0123456789") != std::string::npos)
            throw ProtocolError("invalid IPv4 address: " + address);
        const auto value = std::stoul(token);
        if (value > 255) throw ProtocolError("invalid IPv4 address: " + address);
        result.push_back(static_cast<std::uint8_t>(value));
        if (end != std::string::npos) begin = end + 1;
    }
    return result;
}
std::string ip_string(const Bytes& bytes, std::size_t offset) {
    require_range(bytes, offset, 4);
    return std::to_string(bytes[offset]) + "." + std::to_string(bytes[offset + 1]) +
           "." + std::to_string(bytes[offset + 2]) + "." + std::to_string(bytes[offset + 3]);
}
void check_enum(std::uint32_t value, std::initializer_list<std::uint32_t> allowed) {
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
        throw ProtocolError("invalid register enumeration value: " + std::to_string(value));
}
std::string xml_node(const std::string& xml, const std::string& name) {
    const std::regex expression("<" + name + "(?:\\s[^>]*)?>([\\s\\S]*?)</" + name + "\\s*>");
    std::smatch match;
    if (!std::regex_search(xml, match, expression))
        throw ProtocolError("calibration XML: missing node " + name);
    return match[1].str();
}

} // namespace

std::uint16_t crc16_modbus(const std::uint8_t* data, std::size_t size) {
    static const std::array<std::uint16_t, 256> table = [] {
        std::array<std::uint16_t, 256> values{};
        for (std::uint16_t i = 0; i < 256; ++i) {
            auto crc = i;
            for (int bit = 0; bit < 8; ++bit)
                crc = static_cast<std::uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
            values[i] = crc;
        }
        return values;
    }();
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i)
        crc = static_cast<std::uint16_t>((crc >> 8) ^ table[(crc ^ data[i]) & 0xFF]);
    return crc;
}
std::uint16_t crc16_modbus(const Bytes& data) { return crc16_modbus(data.data(), data.size()); }

std::uint16_t CommandFrame::result_code() const {
    if (type != message_response) return 0;
    if (rw == rw_write) {
        if (data.size()!=2 && !(command==code(Command::BatchControl) && data.size()==4))
            throw ProtocolError("write ACK must contain two bytes (batch control may append a two-byte batch index)");
        return read_u16(data);
    }
    if (rw == rw_read && data.size() == 2 && command != code(Command::Heartbeat))
        return read_u16(data);
    return 0;
}

Bytes encode_command(const CommandFrame& frame) {
    if (frame.data.size() > std::numeric_limits<std::uint32_t>::max())
        throw ProtocolError("command payload exceeds uint32 length");
    Bytes bytes;
    bytes.reserve(command_overhead + frame.data.size());
    append_u16(bytes, command_header);
    append_u16(bytes, frame.device);
    append_u16(bytes, frame.command);
    append_u16(bytes, frame.message_id);
    bytes.push_back(frame.type);
    bytes.push_back(frame.rw);
    append_u32(bytes, static_cast<std::uint32_t>(frame.data.size()));
    bytes.insert(bytes.end(), frame.data.begin(), frame.data.end());
    append_u16(bytes, crc16_modbus(bytes.data() + 2, bytes.size() - 2));
    append_u16(bytes, command_tail);
    return bytes;
}

CommandFrame decode_command(const Bytes& bytes, bool verify_crc) {
    if (bytes.size() < command_overhead) throw ProtocolError("command frame is shorter than 18 bytes");
    if (read_u16(bytes) != command_header) throw ProtocolError("invalid command header");
    if (read_u16(bytes, bytes.size() - 2) != command_tail) throw ProtocolError("invalid command tail");
    const auto length = read_u32(bytes, 10);
    if (length != bytes.size() - command_overhead) throw ProtocolError("command payload length mismatch");
    const auto crc = read_u16(bytes, bytes.size() - 4);
    if (verify_crc && crc != crc16_modbus(bytes.data() + 2, bytes.size() - 6))
        throw ProtocolError("command CRC mismatch");
    CommandFrame result;
    result.device = read_u16(bytes, 2);
    result.command = read_u16(bytes, 4);
    result.message_id = read_u16(bytes, 6);
    result.type = bytes[8];
    result.rw = bytes[9];
    result.crc = crc;
    result.data.assign(bytes.begin() + 14, bytes.end() - 4);
    return result;
}

Bytes encode_transport(const TransportPacket& packet, bool compute_crc) {
    if (packet.data.size() > std::numeric_limits<std::uint16_t>::max())
        throw ProtocolError("transport payload exceeds uint16 length");
    validate_packet_numbers(packet);
    Bytes bytes;
    bytes.reserve(transport_overhead + packet.data.size());
    append_u16(bytes, transport_header);
    append_u16(bytes, packet.device);
    append_u16(bytes, packet.frame_id);
    append_u16(bytes, packet.total);
    append_u16(bytes, packet.current);
    append_u32(bytes, packet.retransmit);
    append_u32(bytes, packet.reserved);
    append_u16(bytes, static_cast<std::uint16_t>(packet.data.size()));
    bytes.insert(bytes.end(), packet.data.begin(), packet.data.end());
    append_u16(bytes, compute_crc ? crc16_modbus(bytes.data() + 2, bytes.size() - 2) : 0);
    append_u16(bytes, transport_tail);
    return bytes;
}

TransportPacket decode_transport(const Bytes& bytes, bool verify_crc,
                                 bool allow_tail_mismatch, bool allow_length_mismatch) {
    if (bytes.size() < transport_overhead) throw ProtocolError("transport frame is shorter than 24 bytes");
    if (read_u16(bytes) != transport_header) throw ProtocolError("invalid transport header");
    const bool bad_tail = read_u16(bytes, bytes.size() - 2) != transport_tail;
    if (bad_tail && !allow_tail_mismatch) throw ProtocolError("invalid transport tail");
    TransportPacket result;
    result.device = read_u16(bytes, 2);
    result.frame_id = read_u16(bytes, 4);
    result.total = read_u16(bytes, 6);
    result.current = read_u16(bytes, 8);
    result.retransmit = read_u32(bytes, 10);
    result.reserved = read_u32(bytes, 14);
    const auto length = read_u16(bytes, 18);
    const auto available = bytes.size() - transport_overhead;
    result.crc = read_u16(bytes, bytes.size() - 4);
    validate_packet_numbers(result);
    if ((!allow_length_mismatch || bad_tail) && length != available)
        throw ProtocolError("transport payload length mismatch");
    if (bad_tail && (result.device > 4 || result.retransmit != 0 || result.reserved != 0))
        throw ProtocolError("invalid fields prevent transport tail recovery");
    if ((verify_crc || bad_tail) && result.crc != crc16_modbus(bytes.data() + 2, bytes.size() - 6))
        throw ProtocolError("transport CRC mismatch");
    result.data.assign(bytes.begin() + 20,
                       bytes.begin() + 20 + std::min<std::size_t>(length, available));
    return result;
}

std::vector<Bytes> split_transport(const Bytes& data, std::uint16_t device,
                                   std::uint16_t frame_id, std::size_t max_payload,
                                   bool compute_crc) {
    if (max_payload == 0 || max_payload > std::numeric_limits<std::uint16_t>::max())
        throw ProtocolError("transport fragment size must be in 1..65535");
    const auto total = data.empty() ? 1 : (data.size() - 1) / max_payload + 1;
    if (total > std::numeric_limits<std::uint16_t>::max())
        throw ProtocolError("transport fragment count exceeds 65535");
    std::vector<Bytes> result;
    result.reserve(total);
    for (std::size_t i = 0; i < total; ++i) {
        TransportPacket packet;
        packet.device = device;
        packet.frame_id = frame_id;
        packet.total = static_cast<std::uint16_t>(total);
        packet.current = static_cast<std::uint16_t>(i + 1);
        const auto begin = std::min(i * max_payload, data.size());
        const auto length = std::min(max_payload, data.size() - begin);
        packet.data.assign(data.begin() + begin, data.begin() + begin + length);
        result.push_back(encode_transport(packet, compute_crc));
    }
    return result;
}

const std::vector<CommandSpec>& command_specs() {
    static const std::vector<CommandSpec> specs = {
        {0x0001,"曝光时间",4,4,false,false,DeviceTarget::Camera},
        {0x0002,"增益",4,4,false,false,DeviceTarget::Camera},
        {0x0003,"X 轴 ROI",4,4,false,false,DeviceTarget::Camera},
        {0x0004,"Z 轴 ROI",4,4,false,false,DeviceTarget::Camera},
        {0x0005,"CMOS 区域",16,16,false,false,DeviceTarget::Camera},
        {0x0006,"四路曝光延迟 (20 ns/计数)",16,16,false,false,DeviceTarget::Fpga},
        {0x0007,"相机固件版本",0,4,true,false,DeviceTarget::Camera},
        {0x0008,"相机位置编号",4,4,false,false,DeviceTarget::Camera},
        {0x0009,"相机序列号",4,4,false,false,DeviceTarget::Camera},
        {0x000A,"采样周期 (50 MHz)",4,4,false,false,DeviceTarget::Fpga},
        {0x000B,"相机电源 (0=上电)",4,4,false,false,DeviceTarget::Camera},
        {0x000C,"图像模式",4,4,false,false,DeviceTarget::Camera},
        {0x000D,"行发送间隔",4,4,false,false,DeviceTarget::Camera},
        {0x000E,"激光器开关",4,0,false,true,DeviceTarget::Camera},
        {0x000F,"激光亮度",4,4,false,false,DeviceTarget::Camera},
        {0x0010,"激光提前/关闭延迟",8,8,false,false,DeviceTarget::Camera},
        {0x0011,"帧等待时间",4,4,false,false,DeviceTarget::Camera},
        {0x0012,"HDR 拐点 (协议 2026-09-23)",8,8,false,false,DeviceTarget::Camera},
        {0x0013,"HDR 长/短曝光 (协议 2026-09-23)",8,8,false,false,DeviceTarget::Camera},
        {0x0101,"触发模式",4,4,false,false,DeviceTarget::Fpga},
        {0x0102,"编码器类型",4,4,false,false,DeviceTarget::Fpga},
        {0x0103,"编码器输入模式",4,4,false,false,DeviceTarget::Fpga},
        {0x0104,"编码器触发间隔",4,4,false,false,DeviceTarget::Fpga},
        {0x0201,"批处理长度",4,4,false,false,DeviceTarget::Camera},
        {0x0202,"外部触发门控",4,4,false,false,DeviceTarget::Fpga},
        {0x0203,"批处理控制",4,4,false,false,DeviceTarget::Camera},
        {0x0204,"实时批处理数据",0,0,true,false,DeviceTarget::Camera},
        {0x0205,"全部批处理数据",0,0,true,false,DeviceTarget::Camera},
        {0x0206,"重置缓存",2,0,false,true,DeviceTarget::Camera},
        {0x0207,"3D 插值配置 (signed Q16.16)",16,16,false,false,DeviceTarget::Camera},
        {0x0300,"相机数据块 (已重排)",0,0,true,false,DeviceTarget::Camera},
        {0x0301,"相机数据块/图像 (原始)",0,0,true,false,DeviceTarget::Camera},
        {0x0302,"中心线",0,0,true,false,DeviceTarget::Camera},
        {0x0303,"3D 轮廓",0,0,true,false,DeviceTarget::Camera},
        {0x0304,"3D 插值结果",0,0,true,false,DeviceTarget::Camera},
        {0x0401,"心跳/设备状态",0,2,true,false,DeviceTarget::Fpga},
        {0x0501,"网络参数",24,24,false,false,DeviceTarget::Fpga},
        {0x0502,"FPGA 固件版本 (4B / 兼容 8B 字符串)",0,4,true,false,DeviceTarget::Fpga},
        {0x0503,"设备序列号",4,4,false,false,DeviceTarget::Fpga},
        {0x0504,"相机型号",8,8,false,false,DeviceTarget::Camera},
        {0x0505,"标定参数 (64 × signed 48 bit)",384,384,false,false,DeviceTarget::Camera},
        {0x0506,"设备型号",8,8,false,false,DeviceTarget::Fpga}
    };
    return specs;
}
const CommandSpec* command_spec(std::uint16_t command) {
    const auto& specs = command_specs();
    const auto found = std::find_if(specs.begin(), specs.end(),
                                   [command](const CommandSpec& s) { return s.code == command; });
    return found == specs.end() ? nullptr : &*found;
}

void validate_write(std::uint16_t command, const Bytes& payload) {
    const auto* spec = command_spec(command);
    if (!spec) throw ProtocolError("unknown register command");
    if (spec->read_only) throw ProtocolError("command is read-only");
    require_size(payload, spec->write_size, "register payload");
    if (command == code(Command::Set3DInterpolationConfig)) {
        unpack_interpolation_config(payload).validate();
        return;
    }
    if (command == code(Command::BatchControl)) {
        check_enum(read_u16(payload), {0,1});
        return;
    }
    if (command == code(Command::ResetBuffer)) {
        check_enum(read_u16(payload), {1});
        return;
    }
    if (command == code(Command::HdrKnees)) {
        const auto knee1=read_u32(payload), knee2=read_u32(payload,4);
        if (knee1 > 200 || knee2 > 200 || knee2 <= knee1)
            throw ProtocolError("HDR knees must be in 0..200 with knee2 greater than knee1");
        return;
    }
    if (payload.size() != 4) return;
    const auto value = read_u32(payload);
    switch (static_cast<Command>(command)) {
    case Command::RoiX:
        if ((value >> 16) > 16 || (value & 0xFFFF) > 16 ||
            (value >> 16) + (value & 0xFFFF) > 16)
            throw ProtocolError("X ROI must fit the 16 sensor lanes");
        break;
    case Command::CameraPosition: check_enum(value, {1,2,3,4}); break;
    case Command::CameraPower:
    case Command::ImageMode:
    case Command::LaserControl:
    case Command::ExternalTriggerStart: check_enum(value, {0,1}); break;
    case Command::TriggerMode:
    case Command::EncoderMode: check_enum(value, {1,2,3}); break;
    case Command::EncoderType: check_enum(value, {1,2}); break;
    case Command::LineTime:
        if (value < 128) throw ProtocolError("line time must be at least 128 clock counts");
        break;
    case Command::LaserBrightness:
        if (value > 100) throw ProtocolError("laser brightness must be in 0..100");
        break;
    default: break;
    }
}

Bytes default_read_payload(std::uint16_t command) {
    switch (static_cast<Command>(command)) {
    case Command::CameraFirmwareVersion:
    case Command::GetAllBatchData: return {0,1};
    case Command::GetBatchDataRealtime: return {0,0x32};
    case Command::GetCenterline:
    case Command::Get3DData:
    case Command::Get3DInterpolation: return {};
    default: return {0,0};
    }
}
Bytes camera_data_request_payload(bool rearranged) { return pack_u16(rearranged ? 0 : 1); }
std::uint16_t write_ack_result(const CommandFrame& frame, std::uint16_t device,
                                std::uint16_t command) {
    if (frame.device != device || frame.command != command ||
        frame.type != message_response || frame.rw != rw_write)
        throw ProtocolError("write ACK device, command, or type mismatch");
    require_size(frame.data, 2, "write ACK");
    return read_u16(frame.data);
}

Bytes pack_u16(std::uint16_t value) { Bytes out; append_u16(out, value); return out; }
Bytes pack_u32(std::uint32_t value) { Bytes out; append_u32(out, value); return out; }
std::uint16_t read_u16(const Bytes& data, std::size_t offset) {
    require_range(data, offset, 2);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8) | data[offset + 1]);
}
std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
    require_range(data, offset, 4);
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
}
std::int32_t read_i32(const Bytes& data, std::size_t offset) {
    const auto value = read_u32(data, offset);
    // Explicit two's-complement conversion also works on implementations where
    // converting an out-of-range uint32 directly to int32 is implementation-defined.
    return value <= 0x7FFFFFFFU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000LL);
}
Bytes pack_roi(std::uint16_t start, std::uint16_t extent) {
    return pack_u32((static_cast<std::uint32_t>(start) << 16) | extent);
}
Bytes pack_exposure_delays(const std::array<std::uint32_t, 4>& counts) {
    Bytes out;
    for (auto value : counts) append_u32(out, value);
    return out;
}
std::array<std::uint32_t, 4> unpack_exposure_delays(const Bytes& payload) {
    require_size(payload, 16, "exposure delays");
    return {{read_u32(payload), read_u32(payload,4), read_u32(payload,8), read_u32(payload,12)}};
}
Bytes pack_laser_delay(std::uint32_t on_advance, std::uint32_t off_delay) {
    Bytes out; append_u32(out,on_advance); append_u32(out,off_delay); return out;
}
Bytes pack_hdr_knees(std::uint32_t knee1, std::uint32_t knee2) {
    const auto result=pack_laser_delay(knee1,knee2);
    validate_write(code(Command::HdrKnees),result);
    return result;
}
Bytes pack_hdr_exposure(std::uint32_t long_exposure, std::uint32_t short_exposure) {
    return pack_laser_delay(long_exposure,short_exposure);
}
Bytes pack_batch_control(bool start, std::uint16_t batch_index) {
    Bytes out; append_u16(out,start ? 1 : 0); append_u16(out,batch_index); return out;
}
std::uint32_t sample_rate_to_ticks(double rate_hz) {
    if (!std::isfinite(rate_hz) || rate_hz < 0) throw ProtocolError("sample rate must be finite and nonnegative");
    if (rate_hz == 0) return 0;
    const auto ticks = std::floor(sample_clock_hz / rate_hz);
    if (!std::isfinite(ticks) || ticks > std::numeric_limits<std::uint32_t>::max())
        throw ProtocolError("sample period exceeds uint32 clock-count range");
    return static_cast<std::uint32_t>(ticks);
}
double sample_ticks_to_rate(std::uint32_t ticks) {
    if (ticks == 0) return 0;
    const double rate = static_cast<double>(sample_clock_hz) / ticks;
    return rate >= 1 ? round_even(rate) : rate;
}

Bytes pack_cmos_area(const CmosArea& area) {
    Bytes out;
    append_u32(out,static_cast<std::uint32_t>(area.x1));
    append_u32(out,static_cast<std::uint32_t>(area.x2));
    append_u32(out,static_cast<std::uint32_t>(area.y1));
    append_u32(out,static_cast<std::uint32_t>(area.y2));
    return out;
}
CmosArea unpack_cmos_area(const Bytes& payload) {
    require_range(payload,0,16);
    return {read_i32(payload),read_i32(payload,4),read_i32(payload,8),read_i32(payload,12)};
}

Bytes pack_network_parameters(const NetworkParameters& parameters) {
    Bytes out;
    for (const auto* ip : {&parameters.device_ip,&parameters.host_ip,&parameters.mask,&parameters.gateway}) {
        const auto raw = ip_bytes(*ip);
        out.insert(out.end(),raw.begin(),raw.end());
    }
    out.push_back(0); out.push_back(0);
    out.insert(out.end(),parameters.mac.begin(),parameters.mac.end());
    return out;
}
NetworkParameters unpack_network_parameters(const Bytes& payload) {
    require_range(payload,0,24);
    NetworkParameters result;
    result.device_ip = ip_string(payload,0);
    result.host_ip = ip_string(payload,4);
    result.mask = ip_string(payload,8);
    result.gateway = ip_string(payload,12);
    std::copy(payload.begin()+18,payload.begin()+24,result.mac.begin());
    return result;
}

std::int32_t mm_to_q16(double value) {
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() / q16_scale ||
        value > std::numeric_limits<std::int32_t>::max() / q16_scale)
        throw ProtocolError("coordinate exceeds finite signed Q16.16 range");
    return static_cast<std::int32_t>(round_even(value * q16_scale));
}
InterpolationConfig InterpolationConfig::from_mm(double start, double end, double spacing,
                                                 std::uint32_t points) {
    InterpolationConfig result{mm_to_q16(start),mm_to_q16(end),mm_to_q16(spacing),points};
    result.validate();
    return result;
}
void InterpolationConfig::validate() const {
    if (count < 1 || count > max_interpolation_count) throw ProtocolError("interpolation count must be in 1..2048");
    if (x_end <= x_start) throw ProtocolError("interpolation upper bound must exceed the start");
    if (interval < 2) throw ProtocolError("interpolation interval must be at least two Q16.16 LSBs");
    const auto last = static_cast<std::int64_t>(x_start) + static_cast<std::int64_t>(count - 1) * interval;
    if (last > std::numeric_limits<std::int32_t>::max()) throw ProtocolError("last interpolation point exceeds signed Q16.16 range");
    if (last >= x_end) throw ProtocolError("last interpolation point reaches the right-open upper bound");
}
double InterpolationConfig::last_mm() const {
    validate();
    return (static_cast<std::int64_t>(x_start) + static_cast<std::int64_t>(count - 1) * interval) / q16_scale;
}
std::array<double, 2> InterpolationConfig::receive_bounds_mm() const {
    validate();
    const auto half = interval / 2;
    return {{(static_cast<std::int64_t>(x_start) - half) / q16_scale,
             (static_cast<std::int64_t>(x_start) + static_cast<std::int64_t>(count - 1) * interval + half) / q16_scale}};
}
Bytes pack_interpolation_config(const InterpolationConfig& config) {
    config.validate();
    Bytes out;
    append_u32(out,static_cast<std::uint32_t>(config.x_start));
    append_u32(out,static_cast<std::uint32_t>(config.x_end));
    append_u32(out,static_cast<std::uint32_t>(config.interval));
    append_u32(out,config.count);
    return out;
}
InterpolationConfig unpack_interpolation_config(const Bytes& payload) {
    require_size(payload,16,"interpolation config");
    return {read_i32(payload),read_i32(payload,4),read_i32(payload,8),read_u32(payload,12)};
}

Bytes pack_calibration(const std::array<double, calibration_count>& values) {
    Bytes out;
    out.reserve(calibration_size);
    constexpr std::int64_t minimum = -(1LL << 47), maximum = (1LL << 47) - 1;
    for (auto value : values) {
        const double scaled = value * calibration_scale;
        const double rounded = round_even(scaled);
        if (!std::isfinite(scaled) || rounded < minimum || rounded > maximum)
            throw ProtocolError("calibration value exceeds signed 48-bit range");
        const auto integer = static_cast<std::int64_t>(rounded);
        const auto bits = static_cast<std::uint64_t>(integer) & 0xFFFFFFFFFFFFULL;
        for (int shift = 40; shift >= 0; shift -= 8)
            out.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
    return out;
}
std::array<double, calibration_count> unpack_calibration(const Bytes& payload) {
    require_size(payload,calibration_size,"calibration data");
    std::array<double, calibration_count> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        std::uint64_t bits = 0;
        for (std::size_t j = 0; j < 6; ++j) bits = (bits << 8) | payload[i*6+j];
        const auto integer = (bits & (1ULL << 47)) ?
            static_cast<std::int64_t>(bits) - (1LL << 48) : static_cast<std::int64_t>(bits);
        result[i] = integer / calibration_scale;
    }
    return result;
}
Bytes pack_calibration_xml(const std::string& path) {
    std::ifstream input(std::filesystem::u8path(path),std::ios::binary);
    if (!input) throw ProtocolError("cannot open calibration XML: " + path);
    std::ostringstream stream; stream << input.rdbuf();
    const auto calib = xml_node(stream.str(),"calib");
    static const std::array<std::pair<const char*,std::size_t>,8> nodes{{
        {"offsets_input",2},{"gains_input",2},{"mins_input",2},
        {"offsets_output",2},{"gains_output",2},{"mins_output",2},
        {"weight_bias_1",30},{"weight_bias_2",22}
    }};
    std::array<double,calibration_count> values{};
    std::size_t next = 0;
    for (const auto& node : nodes) {
        const auto matrix = xml_node(calib,node.first);
        auto raw = xml_node(matrix,"data");
        std::replace(raw.begin(),raw.end(),',',' ');
        std::istringstream numbers(raw);
        std::size_t count = 0;
        double value = 0;
        while (numbers >> value) {
            if (count >= node.second) throw ProtocolError("calibration XML matrix size mismatch: " + std::string(node.first));
            values[next++] = value;
            ++count;
        }
        if (!numbers.eof() || count != node.second)
            throw ProtocolError("calibration XML matrix size mismatch: " + std::string(node.first));
    }
    return pack_calibration(values);
}
Bytes pack_model(const std::string& model) {
    if (model.size() > 8) throw ProtocolError("model must fit in eight bytes");
    Bytes out(model.begin(),model.end());
    out.resize(8,0);
    return out;
}
std::string unpack_model(const Bytes& payload) {
    require_size(payload,8,"model");
    const auto end = std::find(payload.begin(),payload.end(),0);
    return std::string(payload.begin(),end);
}

} // namespace fpga
