#include "fpga/tools.hpp"
#include "../src/socket.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

using namespace fpga;
using namespace std::chrono_literals;
namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void invalid(const std::function<void()>& call) {
    try { call(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid_argument");
}
ConnectionOptions loopback(std::uint16_t port) {
    ConnectionOptions options; options.device_ip = options.local_ip = "127.0.0.1";
    options.device_port = port; options.local_port = 0; return options;
}
struct Peer {
    net::Socket socket = net::bind("127.0.0.1", 0);
    std::atomic_bool running{true};
    std::thread worker;
    explicit Peer(std::function<void(CommandFrame,const sockaddr_in&,net::Socket)> respond) {
        worker = std::thread([this, respond] {
            while (running) {
                if (!net::ready(socket, 2ms)) continue;
                sockaddr_in source{};
                const auto bytes = net::receive(socket, source);
                try {
                    const auto packet = decode_transport(bytes);
                    if (packet.total == 1) respond(decode_command(packet.data), source, socket);
                } catch (const std::exception&) {}
            }
        });
    }
    ~Peer() { running = false; worker.join(); net::close(socket); }
    std::uint16_t port() const { return net::port(socket); }
};
void response(net::Socket socket, const sockaddr_in& destination, CommandFrame frame, const Bytes& payload) {
    frame.type = message_response; frame.data = payload;
    for (const auto& packet : split_transport(encode_command(frame), frame.device, frame.message_id)) net::send(socket, destination, packet);
}
void discovery_tests() {
    Simulator simulator; simulator.start();
    DiscoveryOptions options; options.target_port = simulator.port(); options.local_ip = "127.0.0.1"; options.timeout = 100ms;
    const auto found = discover_devices({"127.0.0.1","127.0.0.1"}, options);
    require(found.sent == 1 && found.devices.size() == 1 && found.local_port, "discovery deduplicates candidates and binds independent ephemeral port");
    require(found.devices[0].ip == "127.0.0.1" && found.devices[0].port == simulator.port() && found.devices[0].status == 0x0155, "discovery heartbeat status is payload, not error result");
    require(ipv4_subnet_candidates("192.168.10.100",2,4) == std::vector<std::string>({"192.168.10.2","192.168.10.3","192.168.10.4"}), "IPv4 /24 candidate list");
    invalid([&] { discover_devices({"example.com"}, options); });
    invalid([] { ipv4_subnet_candidates("192.168.10.1",8,2); });
    std::atomic_bool stop{true};
    require(discover_devices({"127.0.0.1"}, options, &stop).cancelled, "discovery cancellation before network send");
    std::atomic<unsigned> rejected{0};
    Peer identity([&](CommandFrame command,const sockaddr_in& target,net::Socket socket) {
        auto wrong = command; ++wrong.message_id; response(socket,target,wrong,{1,0x55});
        wrong = command; wrong.device = 1; response(socket,target,wrong,{1,0x55});
        wrong = command; wrong.command = 1; response(socket,target,wrong,{1,0x55});
        wrong = command; wrong.rw = rw_write; response(socket,target,wrong,{1,0x55});
        wrong = command; wrong.type = message_event; wrong.data = {1,0x55};
        for (const auto& packet : split_transport(encode_command(wrong),0,wrong.message_id)) net::send(socket,target,packet);
        const auto stranger = net::bind("127.0.0.1",0); response(stranger,target,command,{1,0x55}); net::close(stranger);
        wrong = command; wrong.type = message_response; wrong.data = {0x12,0x34};
        net::send(socket,target,encode_command(wrong)); ++rejected; // Also accept direct AA55 heartbeat responses.
    });
    options.target_port = identity.port();
    const auto strict = discover_devices({"127.0.0.1"}, options);
    require(strict.devices.size() == 1 && strict.devices[0].status == 0x1234 && strict.invalid_packets >= 6, "discovery strict MsgID/device/command/type/rw/source matching");
    require(rejected == 1, "discovery did not retry heartbeat probes");
    Peer silent([](CommandFrame,const sockaddr_in&,net::Socket) {});
    options.target_port = silent.port(); options.timeout = 50ms;
    const auto begin = std::chrono::steady_clock::now();
    const auto timed_out = discover_devices({"127.0.0.1"},options);
    require(timed_out.devices.empty() && timed_out.deadline_expired, "silent discovery candidate times out with explicit deadline status");
    require(std::chrono::steady_clock::now() - begin >= 40ms, "discovery honors bounded whole-scan timeout");
    stop = false;
    std::thread cancel([&] { std::this_thread::sleep_for(10ms); stop = true; });
    options.timeout = 1s;
    const auto aborted = discover_devices({"127.0.0.1"},options,&stop); cancel.join();
    require(aborted.cancelled && aborted.devices.empty(), "active discovery cancellation");
}
void stress_tests() {
    Simulator simulator; simulator.start();
    Device device; device.open(loopback(simulator.port()));
    StressOptions options; options.expected_response = pack_u32(10000); options.count = 30; options.interval_us = 200;
    options.timeout = 150ms; options.max_inflight = 4;
    const auto read = run_register_stress(device,options);
    require(read.sent == 30 && read.ok == 30 && read.requests == 30 && read.peak_inflight <= 4, "bounded asynchronous read stress over real UDP");
    require(read.mean_latency_us > 0 && read.elapsed_ms > 0 && read.mean_send_interval_us > 0, "stress timing measurements are populated");
    options.rw = rw_write; options.request_data = pack_u32(2345); options.expected_response = {0,0};
    options.verify_readback = true; options.expected_readback = options.request_data; options.count = 20;
    const auto writes = run_register_stress(device,options);
    require(writes.sent == 20 && writes.ok == 20 && writes.readbacks == 20 && writes.requests == 40, "write and strict fixed-expectation readback operations");
    require(read_u32(device.read(1,1).frame.data) == 2345, "stress readback leaves requested final register value");
    require(device.statistics().retries == 0 && device.statistics().pending_requests == 0, "stress uses no retry and drains all requests");
    options.expected_readback = pack_u32(3456); options.count = 5;
    const auto mismatch = run_register_stress(device,options);
    require(mismatch.ok == 0 && mismatch.readback_errors == 5, "expected payload is not replaced by observed device output");
    options.verify_readback = false; options.rw = rw_read; options.request_data.clear(); options.expected_response = pack_u32(2345);
    options.count = 1000; options.interval_us = 5000;
    std::atomic_bool stop{false};
    std::thread cancel([&] { std::this_thread::sleep_for(20ms); stop = true; });
    const auto aborted = run_register_stress(device,options,&stop); cancel.join();
    require(aborted.sent < 1000 && aborted.cancelled + aborted.ok == 1000 && device.statistics().pending_requests == 0, "cancellation stops submissions and drains previously-sent operations");
    invalid([&] { auto bad=options; bad.verify_readback=true; run_register_stress(device,bad); });
    device.close();

    std::set<std::uint16_t> messages;
    std::mutex mutex;
    Peer independent([&](CommandFrame frame,const sockaddr_in& peer,net::Socket socket) {
        { std::lock_guard<std::mutex> lock(mutex); messages.insert(frame.message_id); }
        response(socket,peer,frame,pack_u32(42));
    });
    device.open(loopback(independent.port())); options.count=50; options.interval_us=0; options.expected_response=pack_u32(42);
    const auto identifiers=run_register_stress(device,options);
    require(identifiers.ok==50 && messages.size()==50, "every request has an independent MsgID even with zero interval");
    device.close();
    Peer busy([](CommandFrame frame,const sockaddr_in& peer,net::Socket socket) { response(socket,peer,frame,{0,7}); });
    device.open(loopback(busy.port())); options.count=5;
    require(run_register_stress(device,options).device_errors==5, "device errors classified separately");
    device.close();
    std::atomic<unsigned> received{0};
    Peer silent([&](CommandFrame,const sockaddr_in&,net::Socket) { ++received; });
    device.open(loopback(silent.port())); options.count=7; options.max_inflight=3; options.timeout=20ms;
    const auto timeout=run_register_stress(device,options);
    require(timeout.timeouts==7 && received==7 && timeout.ok==0 && timeout.peak_inflight<=3, "timeouts honor window and requests are never retried");
    device.close();
    options.count=3; require(run_register_stress(device,options).network_errors==3, "closed Device failure classified");
}
void export_tests() {
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::path("fpga_tools_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    std::vector<std::filesystem::path> files;
    struct Cleanup {
        const std::filesystem::path& folder; std::vector<std::filesystem::path>& files;
        ~Cleanup() { std::error_code error; for (const auto& file:files) std::filesystem::remove(file,error); std::filesystem::remove(folder,error); }
    } cleanup{folder,files};
    auto path=[&](const char* name) { const auto file=folder/name; files.push_back(file); return file.u8string(); };
    const Image image{3,2,{0,1,2,253,254,255}};
    const auto pgm=path("image.pgm"); save_image_pgm(pgm,image);
    const auto pgm_bytes=load_binary_file(pgm);
    const std::string header="P5\n3 2\n255\n";
    require(Bytes(pgm_bytes.begin(),pgm_bytes.begin()+header.size())==Bytes(header.begin(),header.end()) &&
            Bytes(pgm_bytes.begin()+header.size(),pgm_bytes.end())==image.pixels, "PGM header and Gray8 pixels");
    const auto raw=path("image.raw"); save_image_raw(raw,image); require(load_binary_file(raw)==image.pixels, "raw image exact bytes");
    bool refused=false;
    try { save_image_raw(raw,Image{1,1,{9}}); } catch (const std::runtime_error&) { refused=true; }
    require(refused && load_binary_file(raw)==image.pixels, "exports preserve existing user data by default");
    save_image_raw(raw,Image{1,1,{9}},true); require(load_binary_file(raw)==Bytes{9}, "explicit overwrite is respected");
    const auto center=path("center.csv"); export_points_csv(center,std::vector<Point2>{{1.25,-999},{2.5,0}});
    const auto center_bytes=load_binary_file(center);
    require(std::string(center_bytes.begin(),center_bytes.end())=="x,z\n1.25,-999\n2.5,0\n", "contour CSV preserves sentinel and true zero");
    const auto cloud=path("cloud.csv"); export_points_csv(cloud,std::vector<Point3>{{1,2,3}});
    require(load_binary_file(cloud)==Bytes({'x',',','y',',','z','\n','1',',','2',',','3','\n'}), "point cloud CSV coordinates");
    const auto xyz=path("cloud.xyz"); export_points_xyz(xyz,{{1.25,2.5,-999}});
    const auto xyz_bytes=load_binary_file(xyz); require(std::string(xyz_bytes.begin(),xyz_bytes.end())=="1.25 2.5 -999\n", "XYZ format");
    BatchData batch; batch.x_start_raw=0xfffe0000; batch.x_end_raw=65536; batch.interval_raw=32768; batch.point_count=2;
    batch.records={{0xdeadbeef,0xabcde12345ull,7,{{0x55,0x66}},{1.25,-999}},{3,4,8,{{0,0}},{0,2}}};
    const auto profiles=path("batch.csv"); export_batch_csv(profiles,batch);
    const auto profile_bytes=load_binary_file(profiles); const std::string text(profile_bytes.begin(),profile_bytes.end());
    require(text.find("3735928559,737893491525,7,5566,0,4294836224,65536,32768,1.25")!=std::string::npos,
            "batch export preserves actual frame number, raw 40-bit timestamp, start/stop index and padding");
    require(text.find("3,4,8,0000,0,4294836224,65536,32768,0")!=std::string::npos, "batch export records are not inferred from first frame number");
    invalid([&] { auto bad=batch; bad.records[0].heights.pop_back(); export_batch_csv(path("bad.csv"),bad); });
    invalid([&] { save_image_pgm(path("bad.pgm"),Image{3,2,{0}}); });
}
} // namespace
int main() {
    try { discovery_tests(); stress_tests(); export_tests(); std::cout<<"tools tests passed: discovery identity/cancellation, stress/readback/timeout, safe exports\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
