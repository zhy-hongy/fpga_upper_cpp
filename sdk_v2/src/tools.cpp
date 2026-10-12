#include "fpga/tools.hpp"
#include "socket.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#endif

namespace fpga {
namespace {
using Clock = std::chrono::steady_clock;
bool cancelled(const std::atomic_bool* flag) { return flag && flag->load(std::memory_order_relaxed); }
struct SocketGuard {
    net::Socket socket = net::invalid;
    ~SocketGuard() { net::close(socket); }
};
std::atomic<std::uint32_t> discovery_ids{static_cast<std::uint32_t>(Clock::now().time_since_epoch().count())};

class ExportFile {
public:
    ExportFile(const std::string& path, bool overwrite) {
#ifdef _WIN32
        auto absolute = std::filesystem::absolute(std::filesystem::u8path(path));
        absolute.make_preferred();
        handle_ = CreateFileW(absolute.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create export file: " + path + "; Windows error=" + std::to_string(GetLastError()));
#else
        handle_ = ::open(path.c_str(), O_WRONLY | O_CREAT | (overwrite ? O_TRUNC : O_EXCL), 0666);
        if (handle_ < 0) throw std::runtime_error("cannot create export file: " + path + "; " + std::strerror(errno));
#endif
    }
    ExportFile(const ExportFile&) = delete;
    ExportFile& operator=(const ExportFile&) = delete;
    ~ExportFile() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (handle_ >= 0) ::close(handle_);
#endif
    }
    void write(const std::uint8_t* bytes, std::size_t length) {
        while (length) {
            const auto chunk = std::min<std::size_t>(length, 1024 * 1024);
#ifdef _WIN32
            DWORD written = 0;
            if (!WriteFile(handle_, bytes, static_cast<DWORD>(chunk), &written, nullptr) || !written)
                throw std::runtime_error("cannot write export file");
#else
            const auto written = ::write(handle_, bytes, chunk);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) throw std::runtime_error("cannot write export file");
#endif
            bytes += written; length -= written;
        }
    }
    void write(const std::string& text) { write(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()); }
    void finish() {
#ifdef _WIN32
        const auto handle = handle_; handle_ = INVALID_HANDLE_VALUE;
        if (!CloseHandle(handle)) throw std::runtime_error("cannot close export file");
#else
        const auto handle = handle_; handle_ = -1;
        if (::close(handle)) throw std::runtime_error("cannot close export file");
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
void image_check(const Image& image) {
    if (!image.width || !image.height || std::uint64_t(image.width) * image.height != image.pixels.size())
        throw std::invalid_argument("export image dimensions do not match pixel buffer");
}
std::ostringstream text_stream() {
    std::ostringstream stream; stream.imbue(std::locale::classic()); stream << std::setprecision(17);
    return stream;
}
void record_timing(StressSummary& result, Clock::time_point sent, double& total, std::uint64_t& completed) {
    const auto elapsed = std::chrono::duration<double, std::micro>(Clock::now() - sent).count();
    if (!completed) result.min_latency_us = result.max_latency_us = elapsed;
    else { result.min_latency_us = std::min(result.min_latency_us, elapsed); result.max_latency_us = std::max(result.max_latency_us, elapsed); }
    ++completed; total += elapsed;
}
void status_count(StressSummary& result, Status status) {
    switch (status) {
    case Status::Ok: ++result.ok; break;
    case Status::Timeout: ++result.timeouts; break;
    case Status::Cancelled: ++result.cancelled; break;
    case Status::DeviceError: ++result.device_errors; break;
    case Status::ProtocolError: ++result.protocol_errors; break;
    case Status::NetworkError: ++result.network_errors; break;
    }
}
} // namespace

std::vector<std::string> ipv4_subnet_candidates(const std::string& subnet_address, std::uint8_t first, std::uint8_t last) {
    if (first > last) throw std::invalid_argument("IPv4 candidate range must be increasing");
    net::initialize();
    const auto address = net::address(subnet_address, 0);
    const auto base = ntohl(address.sin_addr.s_addr) & 0xffffff00u;
    std::vector<std::string> result;
    for (std::uint32_t host = first; host <= last; ++host) {
        const auto ip = base | host;
        result.push_back(std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." +
                         std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255));
    }
    return result;
}
DiscoveryResult discover_devices(const std::vector<std::string>& candidates, const DiscoveryOptions& options, const std::atomic_bool* cancel) {
    if (!options.target_port || options.timeout.count() <= 0 || options.probe_interval.count() < 0)
        throw std::invalid_argument("invalid discovery port, timeout or probe interval");
    if (candidates.size() > 65535) throw std::invalid_argument("discovery has more candidates than unique message IDs");
    net::initialize();
    struct Candidate { std::string ip; sockaddr_in peer; std::uint16_t message_id; };
    std::vector<Candidate> valid;
    std::set<std::uint32_t> addresses;
    const auto first_id = discovery_ids.fetch_add(static_cast<std::uint32_t>(candidates.size()) + 1, std::memory_order_relaxed);
    for (const auto& ip : candidates) {
        const auto address = net::address(ip, options.target_port);
        if (addresses.insert(address.sin_addr.s_addr).second)
            valid.push_back({ip, address, static_cast<std::uint16_t>(first_id + valid.size())});
    }
    DiscoveryResult result;
    if (cancelled(cancel)) { result.cancelled = true; result.unprobed = valid.size(); return result; }
    if (valid.empty()) return result;
    SocketGuard socket{net::bind(options.local_ip, options.local_port, 1024 * 1024)};
    result.local_port = net::port(socket.socket);
    std::map<std::uint16_t, std::size_t> pending;
    std::vector<std::optional<DeviceInfo>> found(valid.size());
    std::size_t next = 0;
    const auto deadline = Clock::now() + options.timeout;
    auto next_probe = Clock::now();
    while (Clock::now() < deadline) {
        if (cancelled(cancel)) { result.cancelled = true; break; }
        auto now = Clock::now();
        if (next < valid.size() && now >= next_probe) {
            const auto& candidate = valid[next];
            CommandFrame frame{0, 0x0401, candidate.message_id, message_command, rw_read, default_read_payload(0x0401), 0};
            bool sent = true;
            for (const auto& bytes : split_transport(encode_command(frame), 0, candidate.message_id))
                sent = net::send(socket.socket, candidate.peer, bytes) && sent;
            if (sent) { pending.emplace(candidate.message_id, next); ++result.sent; }
            ++next; next_probe = now + options.probe_interval;
        }
        if (next == valid.size() && pending.empty()) break;
        if (!net::ready(socket.socket, std::chrono::milliseconds{1})) continue;
        sockaddr_in source{};
        const auto datagram = net::receive(socket.socket, source);
        try {
            Bytes command = datagram;
            std::optional<std::uint16_t> transport_device;
            if (datagram.size() >= 2 && datagram[0] == 0xab && datagram[1] == 0xab) {
                const auto packet = decode_transport(datagram);
                if (packet.total != 1 || packet.current != 1 || packet.retransmit != 0) throw ProtocolError("invalid discovery transport packet");
                transport_device = packet.device; command = packet.data;
            }
            const auto frame = decode_command(command);
            const auto expected = pending.find(frame.message_id);
            if (expected == pending.end() || !net::same_peer(source, valid[expected->second].peer) ||
                frame.device != 0 || (transport_device && *transport_device != 0) || frame.command != 0x0401 ||
                frame.type != message_response || frame.rw != rw_read || frame.data.size() != 2) {
                ++result.invalid_packets; continue;
            }
            found[expected->second] = DeviceInfo{valid[expected->second].ip, options.target_port, read_u16(frame.data)};
            pending.erase(expected);
        } catch (const std::exception&) { ++result.invalid_packets; }
    }
    result.unprobed = valid.size() - next;
    result.deadline_expired = !result.cancelled && (!pending.empty() || result.unprobed != 0) && Clock::now() >= deadline;
    for (auto& device : found) if (device) result.devices.push_back(std::move(*device));
    return result;
}

StressSummary run_register_stress(Device& device, const StressOptions& options, const std::atomic_bool* cancel) {
    if (!options.count || !options.max_inflight || options.max_inflight > 65535 || options.timeout.count() <= 0 ||
        options.interval_us > 86400000000ull || options.device > 4 || (options.rw != rw_read && options.rw != rw_write))
        throw std::invalid_argument("invalid register stress count, window, timing or target");
    if (options.verify_readback && options.rw != rw_write) throw std::invalid_argument("readback verification requires a write stress command");
    const auto* spec = command_spec(options.command);
    if (!spec || (options.rw == rw_read && spec->write_only) || (options.rw == rw_write && spec->read_only) ||
        (options.verify_readback && spec->write_only)) throw std::invalid_argument("unsupported stress register command/access");
    if (options.rw == rw_write) validate_write(options.command, options.request_data);
    StressSummary result; result.requested = options.count;
    struct Operation { std::future<Response> future; Clock::time_point started; bool readback; };
    std::vector<Operation> active;
    active.reserve(options.max_inflight);
    const RequestOptions request_options{options.timeout, 0};
    const auto started = Clock::now();
    auto next_send = started, previous_send = started;
    bool stop = false;
    double total_latency = 0, total_intervals = 0;
    std::uint64_t completed = 0, intervals = 0;
    while (result.sent < options.count || !active.empty()) {
        if (!stop && cancelled(cancel)) {
            stop = true; result.cancelled += options.count - result.sent;
        }
        bool progressed = false;
        for (std::size_t i = 0; i < active.size();) {
            if (active[i].future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) { ++i; continue; }
            auto response = active[i].future.get();
            if (response.status == Status::Ok) {
                const auto& expected = active[i].readback ? options.expected_readback : options.expected_response;
                if (response.frame.data != expected) {
                    ++result.readback_errors;
                } else if (options.verify_readback && !active[i].readback) {
                    active[i].future = device.request_async(options.device, options.command, rw_read,
                                                            default_read_payload(options.command), request_options);
                    active[i].readback = true; ++result.requests; ++result.readbacks;
                    progressed = true; ++i; continue;
                } else ++result.ok;
            } else status_count(result, response.status);
            record_timing(result, active[i].started, total_latency, completed);
            active.erase(active.begin() + i); progressed = true;
        }
        if (stop && active.empty()) break;
        const auto now = Clock::now();
        if (!stop && result.sent < options.count && active.size() < options.max_inflight && now >= next_send) {
            result.max_schedule_lateness_us = std::max(result.max_schedule_lateness_us,
                std::chrono::duration<double, std::micro>(now - next_send).count());
            if (result.sent) {
                const auto interval = std::chrono::duration<double, std::micro>(now - previous_send).count();
                if (!intervals) result.min_send_interval_us = result.max_send_interval_us = interval;
                else { result.min_send_interval_us = std::min(result.min_send_interval_us, interval); result.max_send_interval_us = std::max(result.max_send_interval_us, interval); }
                total_intervals += interval; ++intervals;
            }
            previous_send = now;
            auto payload = options.request_data;
            if (options.rw == rw_read && payload.empty()) payload = default_read_payload(options.command);
            active.push_back({device.request_async(options.device, options.command, options.rw, std::move(payload), request_options), now, false});
            ++result.sent; ++result.requests; result.peak_inflight = std::max(result.peak_inflight, active.size());
            const auto interval = std::chrono::microseconds{static_cast<std::int64_t>(options.interval_us)};
            next_send += interval;
            if (next_send < now) next_send = now + interval; // No catch-up burst after a full window or OS scheduling delay.
            progressed = true;
        }
        if (!progressed) {
            auto wait = std::chrono::microseconds{1000};
            if (!stop && result.sent < options.count && active.size() < options.max_inflight && next_send > Clock::now())
                wait = std::min(wait, std::chrono::duration_cast<std::chrono::microseconds>(next_send - Clock::now()));
            if (wait.count() > 0) std::this_thread::sleep_for(wait);
            else std::this_thread::yield();
        }
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    if (completed) result.mean_latency_us = total_latency / completed;
    if (intervals) result.mean_send_interval_us = total_intervals / intervals;
    return result;
}

void save_image_pgm(const std::string& path, const Image& image, bool overwrite) {
    image_check(image);
    ExportFile file(path, overwrite);
    file.write("P5\n" + std::to_string(image.width) + " " + std::to_string(image.height) + "\n255\n");
    file.write(image.pixels.data(), image.pixels.size()); file.finish();
}
void save_image_raw(const std::string& path, const Image& image, bool overwrite) {
    image_check(image); ExportFile file(path, overwrite); file.write(image.pixels.data(), image.pixels.size()); file.finish();
}
void export_points_csv(const std::string& path, const std::vector<Point2>& points, bool overwrite) {
    ExportFile file(path, overwrite); file.write("x,z\n");
    for (const auto& point : points) { auto stream = text_stream(); stream << point.x << ',' << point.z << '\n'; file.write(stream.str()); }
    file.finish();
}
void export_points_csv(const std::string& path, const std::vector<Point3>& points, bool overwrite) {
    ExportFile file(path, overwrite); file.write("x,y,z\n");
    for (const auto& point : points) { auto stream = text_stream(); stream << point.x << ',' << point.y << ',' << point.z << '\n'; file.write(stream.str()); }
    file.finish();
}
void export_points_xyz(const std::string& path, const std::vector<Point3>& points, bool overwrite) {
    ExportFile file(path, overwrite);
    for (const auto& point : points) { auto stream = text_stream(); stream << point.x << ' ' << point.y << ' ' << point.z << '\n'; file.write(stream.str()); }
    file.finish();
}
void export_batch_csv(const std::string& path, const BatchData& batch, bool overwrite) {
    if (batch.result_code) throw std::invalid_argument("cannot export an error-only batch response as profile data");
    for (const auto& record : batch.records)
        if (record.timestamp > 0xffffffffffull || record.heights.size() != batch.point_count)
            throw std::invalid_argument("batch export record timestamp or point count is invalid");
    ExportFile file(path, overwrite);
    file.write("frame_no,timestamp_raw,batch_index,padding_hex,point_index,x_start_raw,x_end_raw,interval_raw,height_q16_value\n");
    for (const auto& record : batch.records) {
        auto prefix = text_stream();
        prefix << record.frame_no << ',' << record.timestamp << ',' << unsigned(record.batch_index) << ','
               << std::hex << std::setfill('0') << std::setw(2) << unsigned(record.alignment_padding[0])
               << std::setw(2) << unsigned(record.alignment_padding[1]) << ',';
        for (std::size_t i = 0; i < record.heights.size(); ++i) {
            auto row = text_stream(); row << prefix.str() << i << ',' << batch.x_start_raw << ',' << batch.x_end_raw << ','
                                        << batch.interval_raw << ',' << record.heights[i] << '\n';
            file.write(row.str());
        }
    }
    file.finish();
}

} // namespace fpga
