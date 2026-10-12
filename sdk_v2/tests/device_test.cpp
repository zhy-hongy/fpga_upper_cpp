#include "../src/socket.hpp"
#include "fpga/device.hpp"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace fpga;
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void require(bool condition, const char *why) {
  ++checks;
  if (!condition)
    throw std::runtime_error(why);
}
ConnectionOptions loopback(uint16_t port) {
  ConnectionOptions o;
  o.device_ip = "127.0.0.1";
  o.device_port = port;
  o.local_ip = "127.0.0.1";
  o.local_port = 0;
  return o;
}
struct Peer {
  net::Socket socket = net::bind("127.0.0.1", 0);
  std::atomic<bool> running{true};
  std::thread worker;
  std::atomic<unsigned> received{0};
  Peer(std::function<void(const CommandFrame &, const sockaddr_in &,
                          net::Socket, unsigned)>
           f) {
    worker = std::thread([this, f] {
      while (running) {
        if (!net::ready(socket, 5ms))
          continue;
        sockaddr_in peer{};
        auto bytes = net::receive(socket, peer);
        try {
          auto p = decode_transport(bytes);
          auto c = decode_command(p.data);
          f(c, peer, socket, ++received);
        } catch (const std::exception &) {
        }
      }
    });
  }
  ~Peer() {
    running = false;
    worker.join();
    net::close(socket);
  }
  uint16_t port() { return net::port(socket); }
};
void respond(net::Socket s, const sockaddr_in &peer, CommandFrame c,
             Bytes data) {
  c.type = 1;
  c.data = std::move(data);
  for (auto &b : split_transport(encode_command(c), c.device, c.message_id))
    net::send(s, peer, b);
}
} // namespace
int main() {
  try {
    Simulator simulator;
    simulator.start();
    Device device;
    device.open(loopback(simulator.port()));
    auto r = device.read(1, 1);
    require(bool(r) && read_u32(r.frame.data) == 10000, "initial exposure");
    require(bool(device.write_u32(1, 1, 3456)), "write exposure");
    require(read_u32(device.read(1, 1).frame.data) == 3456,
            "readback exposure");
    require(bool(device.set_sample_rate(200)), "sample rate");
    require(read_u32(device.read(0, 10).frame.data) == 250000,
            "50MHz sample ticks");
    require(bool(device.set_exposure_delay({1, 25, 100, 0xffffffff})),
            "exposure delay");
    require(unpack_exposure_delays(device.read(0, 6).frame.data)[3] ==
                0xffffffff,
            "exposure ticks unchanged");
    require(bool(device.set_camera_power(2, false)), "power off");
    require(read_u32(device.read(2, 11).frame.data) == 1,
            "inverse power encoding");
    require(bool(device.set_batch_length(120)), "batch fanout");
    for (uint16_t d = 0; d <= 4; ++d)
      require(read_u32(device.read(d, 0x0201).frame.data) == 120,
              "all five batch lengths");
    require(bool(device.set_batch_control(true, 7)), "batch start");
    for (uint16_t d = 0; d <= 4; ++d)
      require(read_u32(device.read(d, 0x0203).frame.data) == 0x00010007,
              "all five batch controls");
    std::mutex m;
    std::condition_variable ready;
    unsigned batches = 0;
    BatchData last_batch;
    device.on_batch([&](const BatchEvent &e) {
      std::lock_guard<std::mutex> lock(m);
      ++batches;
      last_batch = e.data;
      ready.notify_one();
    });
    require(bool(device.get_batch_data(1)), "batch read");
    {
      std::unique_lock<std::mutex> lock(m);
      require(ready.wait_for(lock, 2s, [&] { return batches > 0; }),
              "batch callback");
      require(last_batch.records.size() == 5 &&
                  last_batch.records[0].batch_index == 7,
              "batch metadata");
      require(last_batch.records[0].timestamp > 0, "batch timestamp");
    }
    require(bool(device.get_camera_data(1, true, {2s, 0})), "0300 request");
    auto frame = device.wait_next_frame(1, 0, 2s);
    require(frame && frame->data.image, "rearranged image callback");
    require(frame->data.image->pixels.size() == 512 * 256, "image dimensions");
    require(frame->data.centerline.size() == 512 &&
                frame->data.points3d.size() == 128 &&
                frame->data.interpolated.size() == 128,
            "all composite blocks");
    auto old_sequence = frame->sequence;
    require(bool(device.get_camera_data(1, false, {2s, 0})),
            "0300 to0301 request");
    frame = device.wait_next_frame(1, old_sequence, 2s);
    require(frame && frame->command == 0x0301,
            "alternate image reply matches request");
    require(frame->data.image && frame->data.image->pixels[0] < 50,
            "raw lane decode");
    for (uint16_t cmd = 0x0302; cmd <= 0x0304; ++cmd) {
      old_sequence = frame->sequence;
      require(bool(device.read(1, cmd)), "numeric request");
      frame = device.wait_next_frame(1, old_sequence, 2s);
      require(frame && frame->command == cmd, "numeric callback routing");
    }
    device.start_capture(0xf, false, 10ms);
    for (uint16_t d = 1; d <= 4; ++d)
      require(bool(device.wait_next_frame(d, d == 1 ? frame->sequence : 0, 4s)),
              "four sensors flow controlled capture");
    device.stop_capture();
    require(device.statistics().decoded_images >= 5, "image count");
    require(device.statistics().pending_requests == 0,
            "stop clears outstanding frames");
    device.heartbeat();
    std::this_thread::sleep_for(50ms);
    require(device.statistics().pending_requests == 0, "fire forget heartbeat");
    for (unsigned n = 0; n < 5; ++n) {
      device.close();
      device.open(loopback(simulator.port()));
      require(bool(device.read(0, 0x0401)),
              "reconnect heartbeat status is not error");
    }
    device.close();
    require(device.read(1, 1).status == Status::NetworkError,
            "closed request resolves");
    simulator.stop();

    // Wrong source, msg type, device, access and command must not complete a
    // request.
    Peer identity([](const CommandFrame &request, const sockaddr_in &peer,
                     net::Socket s, unsigned) {
      auto c = request;
      c.device = 2;
      respond(s, peer, c, pack_u32(999));
      c = request;
      c.command = 2;
      respond(s, peer, c, pack_u32(999));
      c = request;
      c.rw = 0x40;
      respond(s, peer, c, {0, 0});
      c = request;
      c.type = 2;
      c.data = pack_u32(999);
      for (auto &b : split_transport(encode_command(c), c.device, c.message_id))
        net::send(s, peer, b);
      auto stranger = net::bind("127.0.0.1", 0);
      respond(stranger, peer, request, pack_u32(999));
      net::close(stranger);
      respond(s, peer, request, pack_u32(123));
    });
    device.open(loopback(identity.port()));
    r = device.read(1, 1, {200ms, 0});
    require(bool(r) && read_u32(r.frame.data) == 123,
            "identity/source matching");
    require(device.statistics().unmatched_responses >= 3,
            "identity rejection tracked");
    device.close();

    Peer retry([](const CommandFrame &c, const sockaddr_in &peer, net::Socket s,
                  unsigned n) {
      if (n > 1)
        respond(s, peer, c, pack_u32(42));
    });
    device.open(loopback(retry.port()));
    r = device.read(1, 1, {30ms, 2});
    require(bool(r) && read_u32(r.frame.data) == 42, "retry success");
    require(device.statistics().retries == 1, "only one retry");
    device.close();
    Peer silence([](const CommandFrame &, const sockaddr_in &, net::Socket,
                    unsigned) {});
    device.open(loopback(silence.port()));
    r = device.read(1, 1, {20ms, 1});
    require(r.status == Status::Timeout, "bounded timeout");
    require(silence.received == 2, "exact retry count");
    auto future = device.request_async(1, 1, 0x80, {0, 0}, {5s, 3});
    device.close();
    require(future.get().status == Status::Cancelled,
            "close cancels pending future");

    Peer bad_size([](const CommandFrame &c, const sockaddr_in &peer,
                     net::Socket s, unsigned) {
      respond(s, peer, c, {1, 2, 3});
    });
    device.open(loopback(bad_size.port()));
    require(device.read(1, 1, {100ms, 0}).status == Status::ProtocolError,
            "register read size validation");
    device.close();

    Peer empty_value([](const CommandFrame &c, const sockaddr_in &peer,
                        net::Socket s, unsigned) { respond(s, peer, c, {0, 0}); });
    device.open(loopback(empty_value.port()));
    require(device.read(1, 1, {100ms, 0}).status == Status::ProtocolError,
            "zero result code cannot replace a fixed-width register value");
    require(bool(device.read(1, 0x0300, {100ms, 0})),
            "variable camera response may report no data");
    device.close();

    Peer versions([](const CommandFrame &c, const sockaddr_in &peer,
                     net::Socket s, unsigned n) {
      respond(s, peer, c, n == 1 ? Bytes{1, 2, 3, 4}
                                : Bytes{'O', 'P', 'T', '-', '0', '1', '2', '3'});
    });
    device.open(loopback(versions.port()));
    r = device.read(0, 0x0502, {100ms, 0});
    require(bool(r) && r.frame.data.size() == 4,
            "four-byte FPGA version remains supported");
    r = device.read(0, 0x0502, {100ms, 0});
    require(bool(r) && r.frame.data.size() == 8,
            "eight-byte Python FPGA version remains supported");
    device.close();

    // The scheduler may already have selected a camera when stop is called.
    // Stopping a generation must prevent that selection from sending afterward.
    device.open(loopback(silence.port()));
    for (unsigned n = 0; n < 100; ++n) {
      device.start_capture(0xf, false, 0ms);
      if (n % 4 == 0)
        std::this_thread::sleep_for(1ms);
      device.stop_capture();
    }
    const auto stopped_packets = device.statistics().tx_packets;
    std::this_thread::sleep_for(50ms);
    require(device.statistics().tx_packets == stopped_packets,
            "stopped capture generation never schedules another UDP request");
    require(device.statistics().pending_requests == 0,
            "rapid capture stop clears every pending camera");
    device.close();

    auto capped = loopback(silence.port());
    capped.max_pending_requests = 2;
    device.open(capped);
    auto first_pending = device.request_async(1, 1, rw_read, {0, 0}, {5s, 0});
    auto second_pending = device.request_async(2, 1, rw_read, {0, 0}, {5s, 0});
    auto excess_pending = device.request_async(3, 1, rw_read, {0, 0}, {5s, 0});
    require(excess_pending.wait_for(100ms) == std::future_status::ready,
            "pending count rejection resolves immediately");
    require(excess_pending.get().status == Status::ProtocolError,
            "pending request count limit rejects excess work");
    require(device.statistics().pending_requests == 2,
            "count cap preserves the accepted requests");
    device.close();
    require(first_pending.get().status == Status::Cancelled &&
                second_pending.get().status == Status::Cancelled,
            "closing a capped device cancels accepted requests");

    capped.max_pending_requests = 10;
    capped.max_pending_bytes = 90;
    device.open(capped);
    auto byte_pending = device.request_async(1, 1, rw_read, {0, 0}, {5s, 0});
    auto excess_bytes = device.request_async(2, 1, rw_read, {0, 0}, {5s, 0});
    require(excess_bytes.wait_for(100ms) == std::future_status::ready &&
                excess_bytes.get().status == Status::ProtocolError,
            "pending encoded-byte budget rejects excess work");
    require(device.statistics().pending_requests == 1,
            "byte cap preserves the accepted request");
    device.close();
    require(byte_pending.get().status == Status::Cancelled,
            "byte budget request resolves on close");

    // Concurrent close/open and dimension updates use the same lifetime and
    // state locks. An external CTest timeout catches any join/lock deadlock.
    device.open(loopback(silence.port()));
    std::atomic<unsigned> lifecycle_errors{0};
    std::thread reopen([&] {
      try {
        for (unsigned n = 0; n < 20; ++n) {
          device.close();
          device.open(loopback(silence.port()));
        }
      } catch (...) {
        ++lifecycle_errors;
      }
    });
    std::thread concurrent_close([&] {
      try {
        for (unsigned n = 0; n < 20; ++n) {
          device.close();
          std::this_thread::yield();
        }
      } catch (...) {
        ++lifecycle_errors;
      }
    });
    std::thread dimensions([&] {
      try {
        for (unsigned n = 0; n < 500; ++n)
          device.set_image_dimensions(uint16_t(n % 4 + 1), 512, 256);
      } catch (...) {
        ++lifecycle_errors;
      }
    });
    reopen.join();
    concurrent_close.join();
    dimensions.join();
    require(lifecycle_errors == 0,
            "concurrent lifecycle and dimension updates complete without errors");
    device.close();
    const auto closed_wait_start = std::chrono::steady_clock::now();
    require(!device.wait_next_frame(1, 0, 5s),
            "closed frame wait resolves without waiting for its timeout");
    require(std::chrono::steady_clock::now() - closed_wait_start < 500ms,
            "closed frame wait returns immediately");

    // Reversed fragments, a duplicate, and conflicting duplicate content.
    Peer reordered([](const CommandFrame &request, const sockaddr_in &peer,
                      net::Socket s, unsigned n) {
      auto c = request;
      c.type = 1;
      c.command = 0x0505;
      c.data = Bytes(384, uint8_t(n));
      auto parts =
          split_transport(encode_command(c), c.device, c.message_id, 80);
      net::send(s, peer, parts.back());
      net::send(s, peer, parts.back());
      if (n == 2) {
        auto packet = decode_transport(parts.back());
        packet.data[0] ^= 1;
        net::send(s, peer, encode_transport(packet));
      }
      for (auto it = parts.rbegin() + 1; it != parts.rend(); ++it)
        net::send(s, peer, *it);
    });
    device.open(loopback(reordered.port()));
    r = device.read(1, 0x0505, {200ms, 0});
    require(bool(r) && r.frame.data == Bytes(384, 1),
            "out of order reassembly");
    require(device.statistics().duplicate_packets >= 1,
            "duplicate packet accounting");
    r = device.read(1, 0x0505, {40ms, 0});
    require(r.status == Status::Timeout, "conflicting duplicate rejected");
    device.close();
    Peer incomplete([](const CommandFrame &c, const sockaddr_in &peer,
                       net::Socket s, unsigned) {
      auto reply = c;
      reply.type = 1;
      reply.data = Bytes(384, 0);
      auto parts =
          split_transport(encode_command(reply), c.device, c.message_id, 50);
      net::send(s, peer, parts.front());
    });
    auto o = loopback(incomplete.port());
    o.assembly_timeout = 20ms;
    device.open(o);
    r = device.read(1, 0x0505, {60ms, 0});
    require(r.status == Status::Timeout, "missing packet cannot decode");
    require(device.statistics().incomplete_frames == 1 &&
                device.statistics().assembly_bytes == 0,
            "assembly expiry frees memory");
    device.close();
    std::cout << "Device: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
  }
}
