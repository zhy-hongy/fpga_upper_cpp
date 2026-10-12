#pragma once
#include "fpga/wire.hpp"
#include "fpga/data.hpp"
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace fpga {
enum class Status { Ok, Timeout, Cancelled, DeviceError, ProtocolError, NetworkError };
struct Response {
    Status status = Status::Ok;
    CommandFrame frame;
    std::string error;
    explicit operator bool() const { return status == Status::Ok; }
};
struct RequestOptions {
    std::chrono::milliseconds timeout{1000};
    unsigned retries = 3;
};
struct ConnectionOptions {
    std::string device_ip = "192.168.10.10";
    uint16_t device_port = 60000;
    std::string local_ip = "0.0.0.0";
    uint16_t local_port = 60000;
    // Zero means the shared control/data socket. A distinct port is optional.
    uint16_t data_port = 0;
    bool verify_crc = true;
    bool allow_transport_tail_mismatch = true;
    size_t receive_buffer = 128 * 1024 * 1024;
    size_t max_frame_bytes = 64 * 1024 * 1024;
    size_t max_assembly_bytes = 128 * 1024 * 1024;
    size_t decode_queue_size = 16;
    size_t max_pending_requests = 1024;
    size_t max_pending_bytes = 128 * 1024 * 1024;
    size_t max_decode_bytes = 128 * 1024 * 1024;
    std::chrono::milliseconds assembly_timeout{3000};
};
struct Statistics {
    uint64_t tx_packets=0, rx_packets=0, tx_bytes=0, rx_bytes=0;
    uint64_t assembled_frames=0, malformed_packets=0, incomplete_frames=0;
    uint64_t duplicate_packets=0, dropped_events=0, decoded_images=0;
    uint64_t request_timeouts=0, retries=0, unmatched_responses=0;
    size_t pending_requests=0, assembly_bytes=0, queued_events=0;
};
struct CameraEvent {
    uint16_t device=0, message_id=0, command=0;
    uint64_t sequence=0;
    CameraData data;
};
struct BatchEvent {
    uint16_t device=0, message_id=0, command=0;
    BatchData data;
};

// Owns its UDP sockets and worker threads. No Qt event loop is required.
// Commands may be called from any application thread. Callbacks run on the
// bounded decode worker: they may submit async commands, but must not block
// awaiting a frame or destroy this Device from within a callback.
class Device {
public:
    Device();
    ~Device();
    Device(const Device&)=delete;
    Device& operator=(const Device&)=delete;
    void open(const ConnectionOptions& = {});
    void close();
    bool is_open() const;
    uint16_t local_port() const;
    std::future<Response> request_async(uint16_t device, uint16_t command,
                                      uint8_t rw, Bytes data,
                                      RequestOptions options = {});
    Response request(uint16_t device, uint16_t command, uint8_t rw,
                     const Bytes& data, RequestOptions options = {});
    Response read(uint16_t device, uint16_t command, RequestOptions options = {});
    Response write(uint16_t device, uint16_t command, const Bytes& data,
                   RequestOptions options = {});
    Response write_u32(uint16_t device, uint16_t command, uint32_t value,
                       RequestOptions options = {});
    Response set_sample_rate(double hz);
    Response set_exposure_delay(const std::array<uint32_t,4>& ticks);
    Response set_camera_power(uint16_t device, bool on);
    Response set_roi(uint16_t device, bool x_axis, uint16_t start, uint16_t count);
    Response set_batch_length(uint32_t count, const std::vector<uint16_t>& devices={0,1,2,3,4});
    Response set_batch_control(bool start, uint16_t index=0,
                              const std::vector<uint16_t>& devices={0,1,2,3,4});
    Response reset_buffer(uint16_t device);
    Response get_batch_data(uint16_t device, bool all=false);
    Response get_camera_data(uint16_t device, bool rearranged=false, RequestOptions options={});
    // Flow-controlled 0x0300 polling: at most one outstanding frame per sensor.
    void start_capture(uint8_t sensor_mask=0x0f, bool rearranged=false,
                       std::chrono::milliseconds interval=std::chrono::milliseconds{40});
    void stop_capture();
    void heartbeat(); // fire-and-forget; no timeout retry storm
    void set_image_dimensions(uint16_t device, uint32_t width, uint32_t height);
    std::shared_ptr<const CameraEvent> latest_frame(uint16_t device) const;
    std::shared_ptr<const CameraEvent> wait_next_frame(uint16_t device, uint64_t after_sequence,
                                std::chrono::milliseconds timeout=std::chrono::milliseconds{1000});
    void on_response(std::function<void(const Response&)>);
    void on_camera(std::function<void(std::shared_ptr<const CameraEvent>)>);
    void on_batch(std::function<void(const BatchEvent&)>);
    void on_error(std::function<void(const std::string&)>);
    Statistics statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Explicit loopback simulator, never activated for a hardware connection.
class Simulator {
public:
    Simulator();
    ~Simulator();
    void start(uint16_t port=0);
    void stop();
    uint16_t port() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
