#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace fpga {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint16_t command_header = 0xAA55;
inline constexpr std::uint16_t command_tail = 0xFAFA;
inline constexpr std::uint16_t transport_header = 0xABAB;
inline constexpr std::uint16_t transport_tail = 0xDCDC;
inline constexpr std::size_t command_overhead = 18;
inline constexpr std::size_t transport_overhead = 24;
inline constexpr std::size_t max_transport_data = 1448;
inline constexpr std::uint8_t message_command = 0x00;
inline constexpr std::uint8_t message_response = 0x01;
inline constexpr std::uint8_t message_event = 0x02;
inline constexpr std::uint8_t rw_write = 0x40;
inline constexpr std::uint8_t rw_read = 0x80;
inline constexpr std::uint32_t sample_clock_hz = 50000000;
inline constexpr std::uint32_t max_interpolation_count = 2048;
inline constexpr double q16_scale = 65536.0;
inline constexpr std::size_t calibration_count = 64;
inline constexpr std::size_t calibration_size = 384;
inline constexpr double calibration_scale = 100000000000.0;

enum class DeviceTarget { Fpga, Camera };
enum class Command : std::uint16_t {
    ExposureTime = 0x0001, Gain = 0x0002, RoiX = 0x0003, RoiZ = 0x0004,
    CmosArea = 0x0005, ExposureDelay = 0x0006, CameraFirmwareVersion = 0x0007,
    CameraPosition = 0x0008, CameraSerialNumber = 0x0009, SampleRate = 0x000A,
    CameraPower = 0x000B, ImageMode = 0x000C, LineTime = 0x000D,
    LaserControl = 0x000E, LaserBrightness = 0x000F, LaserDelay = 0x0010,
    ExposureWaitTime = 0x0011, HdrKnees = 0x0012, HdrExposure = 0x0013,
    TriggerMode = 0x0101, EncoderType = 0x0102, EncoderMode = 0x0103,
    EncoderInterval = 0x0104,
    BatchLength = 0x0201, ExternalTriggerStart = 0x0202, BatchControl = 0x0203,
    GetBatchDataRealtime = 0x0204, GetAllBatchData = 0x0205, ResetBuffer = 0x0206,
    Set3DInterpolationConfig = 0x0207,
    GetCameraData = 0x0300, GetCameraImage = 0x0301, GetCenterline = 0x0302,
    Get3DData = 0x0303, Get3DInterpolation = 0x0304,
    Heartbeat = 0x0401, NetworkParameter = 0x0501, FpgaFirmwareVersion = 0x0502,
    DeviceSerialNumber = 0x0503, CameraModel = 0x0504, CalibrationData = 0x0505,
    DeviceModel = 0x0506
};

constexpr std::uint16_t code(Command value) { return static_cast<std::uint16_t>(value); }

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CommandFrame {
    std::uint16_t device = 0;
    std::uint16_t command = 0;
    std::uint16_t message_id = 0;
    std::uint8_t type = message_command;
    std::uint8_t rw = rw_read;
    Bytes data;
    std::uint16_t crc = 0;
    // Heartbeat's 2-byte payload is status data, not a result code.
    std::uint16_t result_code() const;
};

struct TransportPacket {
    std::uint16_t device = 0;
    std::uint16_t frame_id = 0;
    std::uint16_t total = 1;
    std::uint16_t current = 1;
    std::uint32_t retransmit = 0;
    std::uint32_t reserved = 0;
    Bytes data;
    std::uint16_t crc = 0;
};

std::uint16_t crc16_modbus(const std::uint8_t* data, std::size_t size);
std::uint16_t crc16_modbus(const Bytes& data);
Bytes encode_command(const CommandFrame& frame);
CommandFrame decode_command(const Bytes& bytes, bool verify_crc = true);
Bytes encode_transport(const TransportPacket& packet, bool compute_crc = true);
// Broken tail markers are accepted only with a valid CRC, normal packet fields,
// a zero reserved word, a device in 0..4, and an exact payload length.
TransportPacket decode_transport(const Bytes& bytes, bool verify_crc = true,
                                 bool allow_tail_mismatch = false,
                                 bool allow_length_mismatch = false);
std::vector<Bytes> split_transport(const Bytes& data, std::uint16_t device,
                                   std::uint16_t frame_id,
                                   std::size_t max_payload = max_transport_data,
                                   bool compute_crc = true);

struct CommandSpec {
    std::uint16_t code;
    const char* name;
    std::size_t write_size;
    std::size_t read_size; // 0 denotes variable-size response / unsupported read.
    bool read_only;
    bool write_only;
    DeviceTarget device_target;
};

const std::vector<CommandSpec>& command_specs();
const CommandSpec* command_spec(std::uint16_t command);
void validate_write(std::uint16_t command, const Bytes& payload);
Bytes default_read_payload(std::uint16_t command);
Bytes camera_data_request_payload(bool rearranged = true);
std::uint16_t write_ack_result(const CommandFrame& frame, std::uint16_t device,
                                std::uint16_t command);

Bytes pack_u16(std::uint16_t value);
Bytes pack_u32(std::uint32_t value);
std::uint16_t read_u16(const Bytes& data, std::size_t offset = 0);
std::uint32_t read_u32(const Bytes& data, std::size_t offset = 0);
std::int32_t read_i32(const Bytes& data, std::size_t offset = 0);
Bytes pack_roi(std::uint16_t start, std::uint16_t extent);
Bytes pack_exposure_delays(const std::array<std::uint32_t, 4>& counts);
std::array<std::uint32_t, 4> unpack_exposure_delays(const Bytes& payload);
Bytes pack_laser_delay(std::uint32_t on_advance, std::uint32_t off_delay);
Bytes pack_hdr_knees(std::uint32_t knee1, std::uint32_t knee2);
Bytes pack_hdr_exposure(std::uint32_t long_exposure, std::uint32_t short_exposure);
Bytes pack_batch_control(bool start, std::uint16_t batch_index = 0);
std::uint32_t sample_rate_to_ticks(double rate_hz);
double sample_ticks_to_rate(std::uint32_t ticks);

struct CmosArea {
    std::int32_t x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};
Bytes pack_cmos_area(const CmosArea& area);
CmosArea unpack_cmos_area(const Bytes& payload);

struct NetworkParameters {
    std::string device_ip = "192.168.10.10";
    std::string host_ip = "192.168.10.100";
    std::string mask = "255.255.255.0";
    std::string gateway = "192.168.10.1";
    std::array<std::uint8_t, 6> mac{{0x02, 0x00, 0x00, 0x00, 0x00, 0x01}};
};
Bytes pack_network_parameters(const NetworkParameters& parameters);
NetworkParameters unpack_network_parameters(const Bytes& payload);

struct InterpolationConfig {
    std::int32_t x_start = 0, x_end = 0, interval = 0;
    std::uint32_t count = 0;
    static InterpolationConfig from_mm(double start, double end, double interval,
                                        std::uint32_t count);
    void validate() const;
    double start_mm() const { return x_start / q16_scale; }
    double end_mm() const { return x_end / q16_scale; }
    double interval_mm() const { return interval / q16_scale; }
    double last_mm() const;
    std::array<double, 2> receive_bounds_mm() const;
};
std::int32_t mm_to_q16(double value);
Bytes pack_interpolation_config(const InterpolationConfig& config);
InterpolationConfig unpack_interpolation_config(const Bytes& payload);

Bytes pack_calibration(const std::array<double, calibration_count>& values);
std::array<double, calibration_count> unpack_calibration(const Bytes& payload);
// OpenCV FileStorage XML order matches the Python application's calibration loader.
Bytes pack_calibration_xml(const std::string& path);
Bytes pack_model(const std::string& model); // UTF-8/ASCII bytes, zero-padded to 8 bytes.
std::string unpack_model(const Bytes& payload);

} // namespace fpga
