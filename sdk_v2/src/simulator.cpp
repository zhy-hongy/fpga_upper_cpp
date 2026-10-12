#include "fpga/device.hpp"
#include "socket.hpp"
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

namespace fpga {
namespace {
void append(Bytes &b, const Bytes &v) { b.insert(b.end(), v.begin(), v.end()); }
void q16(Bytes &b, double v) {
  append(b, pack_u32(static_cast<uint32_t>(
                static_cast<int32_t>(std::round(v * 65536)))));
}
Bytes standard_to_raw(const Bytes &image, uint32_t w, uint32_t h) {
  uint32_t lanes = w / 128, cycles = 128;
  Bytes raw(image.size());
  for (uint32_t pair = 0; pair < h / 2; ++pair)
    for (uint32_t cycle = 0; cycle < cycles; ++cycle)
      for (uint32_t odd = 0; odd < 2; ++odd)
        for (uint32_t lane = 0; lane < lanes; ++lane)
          raw[((pair * cycles + cycle) * 2 + odd) * lanes +
              (lanes - 1 - lane)] =
              image[(pair * 2 + odd) * w + lane * cycles + cycle];
  return raw;
}
} // namespace
struct Simulator::Impl {
  net::Socket socket = net::invalid;
  std::thread worker;
  std::atomic<bool> running{false};
  uint16_t bound_port = 0;
  std::map<uint32_t, Bytes> registers;
  std::array<uint32_t, 5> frame_no{};
  struct Assembly {
    uint16_t total;
    std::vector<Bytes> chunks;
    std::vector<bool> present;
    size_t count = 0, bytes = 0;
    std::chrono::steady_clock::time_point touched;
  };
  std::map<uint32_t, Assembly> assemblies;
  Bytes camera(uint16_t d, bool raw) {
    const uint32_t w = 512, h = 256;
    Bytes image(w * h);
    double phase = frame_no[d] * .035;
    for (uint32_t x = 0; x < w; ++x) {
      double center = 110 + 25 * std::sin(x * .012 + phase + d * .4);
      for (uint32_t y = 0; y < h; ++y) {
        double distance = (y - center) / 1.8;
        image[y * w + x] = uint8_t(std::min(
            255., 12. + 18. * x / w + 215. * std::exp(-distance * distance)));
      }
    }
    Bytes body;
    append(body, pack_u16(w));
    append(body, pack_u16(h));
    append(body, pack_u16(raw ? 2 : 1));
    append(body, raw ? standard_to_raw(image, w, h) : image);
    Bytes payload = pack_camera_data_block(1, body);
    body.clear();
    append(body, pack_u32(w));
    append(body, pack_u16(1));
    for (uint32_t x = 0; x < w; ++x)
      q16(body, 110 + 25 * std::sin(x * .012 + phase + d * .4));
    append(payload, pack_camera_data_block(2, body));
    body.clear();
    append(body, pack_u32(128));
    append(body, pack_u16(1));
    for (uint32_t x = 0; x < 128; ++x) {
      q16(body, -32 + x * .5);
      q16(body, 3 * std::sin(x * .04 + phase + d * .4));
    }
    append(payload, pack_camera_data_block(3, body));
    body.clear();
    q16(body, -32);
    q16(body, 32);
    q16(body, .5);
    append(body, pack_u32(128));
    append(body, pack_u16(1));
    for (uint32_t x = 0; x < 128; ++x)
      q16(body, 3 * std::sin(x * .04 + phase + d * .4));
    append(payload, pack_camera_data_block(4, body));
    ++frame_no[d];
    return payload;
  }
  Bytes numeric(uint16_t cmd, uint16_t d) {
    Bytes b;
    if (cmd == 0x0302) {
      for (uint32_t x = 0; x < 512; ++x) {
        append(b, pack_u16(x));
        append(b, pack_u16(static_cast<uint16_t>(
                      110 + 25 * std::sin(x * .012 + d * .4))));
      }
    }
    if (cmd == 0x0303) {
      for (uint32_t x = 0; x < 512; ++x) {
        append(b, pack_u16(x));
        append(b, pack_u16(static_cast<uint16_t>(
                      110 + 25 * std::sin(x * .012 + d * .4))));
      }
    }
    if (cmd == 0x0304) {
      append(b, pack_u32(0));
      append(b, pack_u32(512));
      append(b, pack_u32(4));
      append(b, pack_u32(256));
      for (uint32_t x = 0; x < 128; ++x)
        append(b, pack_u16(static_cast<uint16_t>(
                      int16_t(300 * std::sin(x * .04 + d * .4)))));
    }
    return b;
  }
  Bytes batch(uint16_t d) {
    BatchData b;
    b.x_start_raw = uint32_t(int32_t(-32 * 65536));
    b.x_end_raw = 32 * 65536;
    b.interval_raw = 32768;
    b.point_count = 128;
    for (uint32_t n = 0; n < 5; ++n) {
      BatchRecord r;
      r.frame_no = ++frame_no[d];
      r.timestamp = uint64_t(r.frame_no) * 50000;
      r.batch_index = uint8_t(read_u32(registers[(d << 16) | 0x0203]) & 0xff);
      for (uint32_t p = 0; p < b.point_count; ++p)
        r.heights.push_back(3 * std::sin(p * .04 + n * .2 + d * .4));
      b.records.push_back(std::move(r));
    }
    return pack_realtime_batch(b);
  }
  void initialize() {
    registers.clear();
    frame_no = {};
    assemblies.clear();
    for (uint16_t d = 0; d <= 4; ++d)
      for (const auto &spec : command_specs()) {
        Bytes value(spec.read_size ? spec.read_size : 4, 0);
        if (spec.read_size == 4)
          value = pack_u32(10);
        registers[(d << 16) | spec.code] = std::move(value);
      }
    for (uint16_t d = 0; d <= 4; ++d) {
      registers[(d << 16) | 1] = pack_u32(10000);
      registers[(d << 16) | 2] = pack_u32(10);
      registers[(d << 16) | 3] = pack_roi(0, 16);
      registers[(d << 16) | 4] = pack_roi(0, 1152);
      registers[(d << 16) | 0xb] = pack_u32(0);
      registers[(d << 16) | 0xc] = pack_u32(0);
      registers[(d << 16) | 0xd] = pack_u32(160);
      registers[(d << 16) | 0xf] = pack_u32(40);
      registers[(d << 16) | 0x101] = pack_u32(1);
      registers[(d << 16) | 0x201] = pack_u32(100);
      registers[(d << 16) | 0x203] = pack_u32(0);
      registers[(d << 16) | 0x402] = pack_u32(0);
      registers[(d << 16) | 0x207] = pack_interpolation_config(
          InterpolationConfig::from_mm(-32, 32, .5, 128));
      registers[(d << 16) | 0x504] = pack_model("CAM-DEMO");
      registers[(d << 16) | 0x506] = pack_model("FPGA-V2");
      registers[(d << 16) | 0x502] = {'O', 'P', 'T', '-', '0', '2', '0', '0'};
      registers[(d << 16) | 7] = {'V', '2', '.', '0'};
      registers[(d << 16) | 8] = pack_u32(d ? d : 1);
      registers[(d << 16) | 0x501] =
          pack_network_parameters(NetworkParameters{});
    }
    registers[0xa] = pack_u32(500000);
    registers[6] = pack_exposure_delays({0, 0, 0, 0});
  }
  void respond(CommandFrame f, const sockaddr_in &peer) {
    if (f.device > 4 || f.type != message_command ||
        (f.rw != rw_read && f.rw != rw_write))
      return;
    f.type = message_response;
    const auto *spec = command_spec(f.command);
    if (!spec) {
      f.data = {0, 6};
    } else if (f.rw == rw_write) {
      try {
        validate_write(f.command, f.data);
        registers[(f.device << 16) | f.command] = f.data;
        f.data = {0, 0};
      } catch (...) {
        f.data = {0, 2};
      }
    } else if (f.command == 0x0401) {
      f.data = {1, 0x55};
    } else if (f.command == 0x0300) {
      bool raw = f.data != Bytes{0, 0};
      f.data = camera(f.device, raw);
      if (raw)
        f.command = 0x0301;
    } else if (f.command >= 0x0302 && f.command <= 0x0304) {
      f.data = numeric(f.command, f.device);
    } else if (f.command == 0x0204 || f.command == 0x0205) {
      f.data = batch(f.device);
    } else
      f.data = registers[(f.device << 16) | f.command];
    for (const auto &p :
         split_transport(encode_command(f), f.device, f.message_id))
      net::send(socket, peer, p);
  }
  void loop() {
    while (running) {
      auto now = std::chrono::steady_clock::now();
      for (auto it = assemblies.begin(); it != assemblies.end();) {
        if (now - it->second.touched > std::chrono::seconds{3})
          it = assemblies.erase(it);
        else
          ++it;
      }
      if (!net::ready(socket, std::chrono::milliseconds{10}))
        continue;
      sockaddr_in peer{};
      auto bytes = net::receive(socket, peer);
      try {
        auto p = decode_transport(bytes);
        if (p.device > 4 || p.retransmit != 0 || p.data.size() > 1448 ||
            p.total > 32768)
          continue;
        uint32_t key = (p.device << 16) | p.frame_id;
        if (p.total == 1) {
          auto f = decode_command(p.data);
          if (f.device == p.device)
            respond(std::move(f), peer);
        } else {
          auto it = assemblies.find(key);
          if (it == assemblies.end()) {
            if (assemblies.size() >= 8) {
              assemblies.clear();
            }
            it = assemblies
                     .emplace(key,
                              Assembly{p.total, std::vector<Bytes>(p.total),
                                       std::vector<bool>(p.total), 0, 0, now})
                     .first;
          }
          auto &a = it->second;
          if (a.total != p.total) {
            assemblies.erase(it);
            continue;
          }
          if (!a.present[p.current - 1]) {
            a.bytes += p.data.size();
            if (a.bytes > 8 * 1024 * 1024) {
              assemblies.erase(it);
              continue;
            }
            a.present[p.current - 1] = true;
            a.chunks[p.current - 1] = std::move(p.data);
            ++a.count;
            a.touched = now;
          }
          if (a.count == a.total) {
            Bytes frame;
            for (const auto &part : a.chunks)
              append(frame, part);
            assemblies.erase(it);
            auto f = decode_command(frame);
            if (f.device == p.device)
              respond(std::move(f), peer);
          }
        }
      } catch (const std::exception &) {
      }
    }
  }
};
Simulator::Simulator() : impl_(new Impl) {}
Simulator::~Simulator() { stop(); }
void Simulator::start(uint16_t port) {
  if (impl_->running)
    throw std::logic_error("Simulator already running");
  impl_->socket = net::bind("127.0.0.1", port);
  impl_->bound_port = net::port(impl_->socket);
  impl_->initialize();
  impl_->running = true;
  impl_->worker = std::thread([this] { impl_->loop(); });
}
void Simulator::stop() {
  impl_->running = false;
  if (impl_->worker.joinable())
    impl_->worker.join();
  net::close(impl_->socket);
  impl_->socket = net::invalid;
}
uint16_t Simulator::port() const { return impl_->bound_port; }
} // namespace fpga
