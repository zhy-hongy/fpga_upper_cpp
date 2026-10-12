#pragma once

#include "fpga/device.hpp"

#include <atomic>

namespace fpga {

struct DeviceInfo {
    std::string ip;
    std::uint16_t port = 0;
    std::uint16_t status = 0; // Raw 0x0401 heartbeat status, not a result code.
};
struct DiscoveryOptions {
    std::uint16_t target_port = 60000;
    std::string local_ip = "0.0.0.0";
    std::uint16_t local_port = 0;
    std::chrono::milliseconds timeout{1500}; // Whole scan deadline, including probe pacing.
    std::chrono::microseconds probe_interval{2000};
};
struct DiscoveryResult {
    std::vector<DeviceInfo> devices;
    std::size_t sent = 0, invalid_packets = 0, unprobed = 0;
    std::uint16_t local_port = 0;
    bool cancelled = false;
    bool deadline_expired = false;
};
std::vector<std::string> ipv4_subnet_candidates(const std::string& subnet_address,
                                               std::uint8_t first = 1, std::uint8_t last = 254);
DiscoveryResult discover_devices(const std::vector<std::string>& candidates,
                                 const DiscoveryOptions& options = {}, const std::atomic_bool* cancel = nullptr);

struct StressOptions {
    std::uint16_t device = 1, command = 0x0001;
    std::uint8_t rw = rw_read;
    Bytes request_data;
    Bytes expected_response; // Exact expected bytes, fixed for the entire run.
    std::uint32_t count = 100;
    std::uint64_t interval_us = 1000;
    std::chrono::milliseconds timeout{500};
    std::size_t max_inflight = 16;
    bool verify_readback = false; // After each successful write, read the same register.
    Bytes expected_readback; // Exact expected read payload; never learned from device output.
};
struct StressSummary {
    std::uint64_t requested = 0, sent = 0, requests = 0, readbacks = 0;
    std::uint64_t ok = 0, device_errors = 0, timeouts = 0, protocol_errors = 0;
    std::uint64_t network_errors = 0, readback_errors = 0, cancelled = 0;
    std::size_t peak_inflight = 0;
    double elapsed_ms = 0;
    double mean_latency_us = 0, min_latency_us = 0, max_latency_us = 0;
    double mean_send_interval_us = 0, min_send_interval_us = 0, max_send_interval_us = 0;
    double max_schedule_lateness_us = 0;
};
// Call from a worker thread. No retries are used. 'sent' counts submitted primary
// operations; 'requests' includes readbacks. Cancellation stops new operations
// and drains already-sent operations within their Device request deadlines.
// Scheduling uses steady_clock and OS waits; it is not hard real-time pacing.
StressSummary run_register_stress(Device& device, const StressOptions& options,
                                 const std::atomic_bool* cancel = nullptr);

// Image raw export contains decoded row-major Gray8 pixels (no metadata header).
// All exports create a new file by default. Set overwrite=true explicitly to
// replace an existing file. Directories must already exist.
void save_image_pgm(const std::string& path, const Image& image, bool overwrite = false);
void save_image_raw(const std::string& path, const Image& image, bool overwrite = false);
void export_points_csv(const std::string& path, const std::vector<Point2>& points, bool overwrite = false);
void export_points_csv(const std::string& path, const std::vector<Point3>& points, bool overwrite = false);
void export_points_xyz(const std::string& path, const std::vector<Point3>& points, bool overwrite = false);
void export_batch_csv(const std::string& path, const BatchData& batch, bool overwrite = false);

} // namespace fpga
