#include "fpga/device.hpp"
#include "socket.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace fpga {
using Clock = std::chrono::steady_clock;
namespace {
Bytes u32(uint32_t n) {
  return {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
}
Response failure(Status status, const std::string &error, CommandFrame f = {}) {
  return {status, std::move(f), error};
}
Bytes read_payload(uint16_t cmd) {
  if (cmd >= 0x0302 && cmd <= 0x0304)
    return {};
  if (cmd == 0x0007 || cmd == 0x0205)
    return {0, 1};
  if (cmd == 0x0204)
    return {0, 50};
  if (cmd == 0x0300)
    return {0, 1};
  return {0, 0};
}
Response classify(CommandFrame f) {
  Response r;
  r.frame = std::move(f);
  const auto &d = r.frame.data;
  bool result = false;
  if (r.frame.rw == 0x40) {
    if (d.size() != 2 && !(r.frame.command == 0x0203 && d.size() == 4))
      return failure(Status::ProtocolError,
                     "Write ACK must contain a 2-byte result", r.frame);
    result = true;
  } else if (d.size() == 2) {
    const auto *spec = command_spec(r.frame.command);
    result = r.frame.command != 0x0401 && (!spec || spec->read_size != 2);
  }
  if (result && d.size() >= 2) {
    uint16_t code = uint16_t(d[0] << 8) | d[1];
    if (code) {
      r.status = Status::DeviceError;
      r.error = "Device result code " + std::to_string(code);
    }
  }
  if (r.status == Status::Ok && r.frame.rw == 0x80 && !result) {
    const auto *spec = command_spec(r.frame.command);
    bool version = r.frame.command == 0x0502;
    if (spec && spec->read_size &&
        (version ? (d.size() != 4 && d.size() != 8)
                 : d.size() != spec->read_size))
      return failure(Status::ProtocolError,
                     "Read response does not match register size", r.frame);
  }
  if (r.status == Status::Ok && r.frame.rw == 0x80 && result) {
    const auto *spec = command_spec(r.frame.command);
    if (spec && spec->read_size > 2)
      return failure(Status::ProtocolError,
                     "Successful register read is missing its value", r.frame);
  }
  return r;
}
} // namespace
struct Device::Impl {
  mutable std::mutex mutex;
  std::mutex lifecycle;
  std::condition_variable events_ready, frame_ready;
  ConnectionOptions options;
  net::Socket socket = net::invalid, data_socket = net::invalid;
  sockaddr_in target{};
  uint16_t bound_port = 0, next_id = 0;
  bool opened = false, stopping = false;
  std::thread receiver, decoder;
  std::thread::id receiver_id, decoder_id;
  struct Pending {
    CommandFrame frame;
    std::vector<Bytes> packets;
    RequestOptions options;
    unsigned retries = 0;
    Clock::time_point deadline;
    std::promise<Response> promise;
    bool capture = false;
    size_t bytes = 0;
  };
  std::map<uint16_t, std::shared_ptr<Pending>> pending;
  size_t pending_bytes = 0;
  std::array<Clock::time_point, 65536> retired{};
  struct Assembly {
    uint16_t total = 0;
    size_t count = 0, bytes = 0;
    Clock::time_point touched;
    std::vector<Bytes> chunks;
    std::vector<bool> present;
  };
  std::map<uint32_t, Assembly> assemblies;
  std::map<uint32_t, Clock::time_point> completed;
  std::deque<Response> events;
  size_t queued_bytes = 0;
  std::array<std::shared_ptr<const CameraEvent>, 5> latest;
  std::array<uint64_t, 5> sequence{};
  std::array<std::pair<uint32_t, uint32_t>, 5> dimensions;
  std::array<ImageLayout, 5> image_layouts;
  std::function<void(const Response &)> response_callback;
  std::function<void(std::shared_ptr<const CameraEvent>)> camera_callback;
  std::function<void(const BatchEvent &)> batch_callback;
  std::function<void(const std::string &)> error_callback;
  Statistics stats;
  uint8_t capture_mask = 0;
  size_t capture_max_inflight = 1;
  uint16_t capture_cursor = 1;
  uint64_t capture_generation = 0;
  bool capture_rearranged = false;
  std::chrono::milliseconds capture_interval{40};
  std::array<bool, 5> capture_pending{};
  std::array<Clock::time_point, 5> next_capture{};

  Impl() {
    std::random_device rd;
    next_id = static_cast<uint16_t>(rd());
    for (auto &d : dimensions)
      d = {2048, 1152};
    image_layouts.fill(ImageLayout::Declared);
  }
  uint16_t allocate_id() {
    auto now = Clock::now();
    for (unsigned n = 0; n < 65536; ++n) {
      uint16_t id = next_id++;
      if (!pending.count(id) && retired[id] <= now)
        return id;
    }
    throw std::runtime_error("Message ID pool exhausted; reduce request rate");
  }
  bool send_packets(const std::vector<Bytes> &packets) {
    for (const auto &p : packets) {
      if (!net::send(socket, target, p))
        return false;
      ++stats.tx_packets;
      stats.tx_bytes += p.size();
    }
    return true;
  }
  void enqueue(Response r) {
    size_t bytes = r.frame.data.size();
    while (!events.empty() &&
           (events.size() >= options.decode_queue_size ||
            queued_bytes + bytes > options.max_decode_bytes)) {
      queued_bytes -= events.front().frame.data.size();
      events.pop_front();
      ++stats.dropped_events;
    }
    if (bytes > options.max_decode_bytes) {
      ++stats.dropped_events;
      return;
    }
    queued_bytes += bytes;
    events.push_back(std::move(r));
    events_ready.notify_one();
  }
  void finish(std::map<uint16_t, std::shared_ptr<Pending>>::iterator it,
              Response r) {
    auto p = it->second;
    pending_bytes -= p->bytes;
    retired[it->first] = Clock::now() + std::chrono::seconds{5};
    pending.erase(it);
    if (p->capture) {
      capture_pending[p->frame.device] = false;
      next_capture[p->frame.device] = Clock::now() + capture_interval;
    }
    p->promise.set_value(std::move(r));
  }
  std::future<Response> submit(uint16_t dev, uint16_t cmd, uint8_t rw,
                               Bytes data, RequestOptions o,
                               bool capture = false, uint64_t generation = 0) {
    auto p = std::make_shared<Pending>();
    auto future = p->promise.get_future();
    std::lock_guard<std::mutex> lock(mutex);
    p->frame = {dev, cmd, 0, 0, rw, std::move(data)};
    if (!opened || stopping) {
      p->promise.set_value(
          failure(Status::NetworkError, "Device is not open", p->frame));
      return future;
    }
    if (capture &&
        (dev < 1 || dev > 4 || generation != capture_generation ||
         !(capture_mask & (1 << (dev - 1))) || capture_pending[dev])) {
      p->promise.set_value(
          failure(Status::Cancelled, "Capture generation changed", p->frame));
      return future;
    }
    if (capture &&
        size_t(std::count(capture_pending.begin(), capture_pending.end(),
                          true)) >= capture_max_inflight) {
      p->promise.set_value(failure(
          Status::Cancelled, "Capture concurrency limit changed", p->frame));
      return future;
    }
    try {
      if (dev > 4 || (rw != 0x40 && rw != 0x80) || o.timeout.count() <= 0 ||
          o.retries > 100)
        throw std::invalid_argument("Invalid request options or identity");
      if (p->frame.data.size() > options.max_frame_bytes - 18)
        throw std::invalid_argument("Request exceeds frame limit");
      // Payload and encoded fragments are both retained for bounded retries.
      size_t encoded = p->frame.data.size() + 18;
      size_t estimate =
          p->frame.data.size() + encoded + ((encoded + 1447) / 1448) * 24;
      if (pending.size() >= options.max_pending_requests ||
          estimate > options.max_pending_bytes - pending_bytes)
        throw std::runtime_error("Pending request memory limit reached");
      p->frame.message_id = allocate_id();
      p->options = o;
      p->retries = o.retries;
      p->capture = capture;
      p->packets =
          split_transport(encode_command(p->frame), dev, p->frame.message_id);
      p->bytes = p->frame.data.size();
      for (const auto &packet : p->packets)
        p->bytes += packet.size();
      p->deadline = Clock::now() + o.timeout;
      pending.emplace(p->frame.message_id, p);
      pending_bytes += p->bytes;
      if (capture)
        capture_pending[dev] = true;
      if (!send_packets(p->packets))
        finish(pending.find(p->frame.message_id),
               failure(Status::NetworkError, "UDP send failed", p->frame));
    } catch (const std::exception &e) {
      p->promise.set_value(failure(Status::ProtocolError, e.what(), p->frame));
    }
    return future;
  }
  bool accept_frame(Bytes bytes, uint16_t transport_device = 0xffff) {
    CommandFrame f;
    try {
      f = decode_command(bytes, options.verify_crc);
      if (f.device > 4 || (f.rw != rw_read && f.rw != rw_write) ||
          (transport_device != 0xffff && f.device != transport_device))
        throw std::invalid_argument("Transport/command device mismatch");
    } catch (const std::exception &) {
      ++stats.malformed_packets;
      return false;
    }
    ++stats.assembled_frames;
    if (f.type != 1 && f.type != 2) {
      ++stats.malformed_packets;
      return false;
    }
    Response r = classify(std::move(f));
    bool accepted = r.status != Status::ProtocolError;
    auto it = pending.find(r.frame.message_id);
    if (it != pending.end()) {
      const auto &req = it->second->frame;
      bool command_matches =
          req.command == r.frame.command ||
          (req.command == 0x0300 && r.frame.command == 0x0301);
      if (r.frame.type == 1 && req.device == r.frame.device &&
          req.rw == r.frame.rw && command_matches)
        finish(it, r);
      else {
        ++stats.unmatched_responses;
        accepted = false;
      }
    } else if (r.frame.command != 0x0401)
      ++stats.unmatched_responses;
    enqueue(std::move(r));
    return accepted;
  }
  void accept_datagram(Bytes bytes) {
    ++stats.rx_packets;
    stats.rx_bytes += bytes.size();
    if (bytes.size() < 2) {
      ++stats.malformed_packets;
      return;
    }
    if (bytes[0] == 0xaa && bytes[1] == 0x55) {
      accept_frame(std::move(bytes));
      return;
    }
    TransportPacket p;
    try {
      p = decode_transport(bytes, options.verify_crc,
                           options.allow_transport_tail_mismatch);
      if (p.device > 4 || p.retransmit != 0)
        throw std::invalid_argument("Unsupported transport identity");
    } catch (const std::exception &) {
      ++stats.malformed_packets;
      return;
    }
    uint32_t key = (uint32_t(p.device) << 16) | p.frame_id;
    auto now = Clock::now();
    if (completed.count(key)) {
      ++stats.duplicate_packets;
      return;
    }
    if (p.total == 1) {
      if (p.data.size() > options.max_frame_bytes) {
        ++stats.malformed_packets;
        return;
      }
      if (accept_frame(std::move(p.data), p.device))
        completed[key] = now;
      return;
    }
    auto found = assemblies.find(key);
    if (found == assemblies.end()) {
      if (assemblies.size() >= 64 ||
          stats.assembly_bytes + p.data.size() > options.max_assembly_bytes) {
        ++stats.incomplete_frames;
        return;
      }
      Assembly a;
      a.total = p.total;
      a.touched = now;
      a.chunks.resize(p.total);
      a.present.resize(p.total);
      found = assemblies.emplace(key, std::move(a)).first;
    }
    auto &a = found->second;
    if (a.total != p.total) {
      stats.assembly_bytes -= a.bytes;
      assemblies.erase(found);
      completed[key] = now;
      ++stats.malformed_packets;
      return;
    }
    size_t idx = p.current - 1;
    if (a.present[idx]) {
      ++stats.duplicate_packets;
      if (a.chunks[idx] != p.data) {
        stats.assembly_bytes -= a.bytes;
        assemblies.erase(found);
        completed[key] = now;
        ++stats.malformed_packets;
      }
      return;
    }
    if (a.bytes + p.data.size() > options.max_frame_bytes ||
        stats.assembly_bytes + p.data.size() > options.max_assembly_bytes) {
      stats.assembly_bytes -= a.bytes;
      assemblies.erase(found);
      completed[key] = now;
      ++stats.incomplete_frames;
      return;
    }
    a.bytes += p.data.size();
    stats.assembly_bytes += p.data.size();
    a.chunks[idx] = std::move(p.data);
    a.present[idx] = true;
    ++a.count;
    a.touched = now;
    if (a.count == a.total) {
      Bytes frame;
      frame.reserve(a.bytes);
      for (const auto &chunk : a.chunks)
        frame.insert(frame.end(), chunk.begin(), chunk.end());
      stats.assembly_bytes -= a.bytes;
      assemblies.erase(found);
      if (accept_frame(std::move(frame), p.device))
        completed[key] = now;
    }
  }
  void tick() {
    auto now = Clock::now();
    for (auto it = assemblies.begin(); it != assemblies.end();) {
      if (now - it->second.touched >= options.assembly_timeout) {
        stats.assembly_bytes -= it->second.bytes;
        it = assemblies.erase(it);
        ++stats.incomplete_frames;
      } else
        ++it;
    }
    for (auto it = completed.begin(); it != completed.end();) {
      if (now - it->second > options.assembly_timeout)
        it = completed.erase(it);
      else
        ++it;
    }
    // Prevent fire-and-forget IDs from accumulating an unbounded tombstone map.
    while (completed.size() > 8192)
      completed.erase(completed.begin());
    for (auto it = pending.begin(); it != pending.end();) {
      auto current = it++;
      auto &p = *current->second;
      if (now < p.deadline)
        continue;
      if (p.retries) {
        --p.retries;
        ++stats.retries;
        p.deadline = now + p.options.timeout;
        if (!send_packets(p.packets))
          finish(current, failure(Status::NetworkError, "UDP retry send failed",
                                  p.frame));
      } else {
        ++stats.request_timeouts;
        Response r = failure(Status::Timeout, "Request timed out", p.frame);
        finish(current, r);
        enqueue(std::move(r));
      }
    }
  }
  void receive_loop() {
    for (;;) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
          break;
      }
      net::Socket sockets[2] = {socket, data_socket};
      for (auto s : sockets) {
        if (s == net::invalid)
          continue;
        if (net::ready(s, std::chrono::milliseconds{s == socket ? 2 : 0})) {
          // Drain a bounded burst so housekeeping and stop remain responsive.
          for (unsigned n = 0; n < 512; ++n) {
            sockaddr_in peer{};
            auto b = net::receive(s, peer);
            if (b.empty())
              break;
            if (net::same_peer(peer, target)) {
              std::lock_guard<std::mutex> lock(mutex);
              accept_datagram(std::move(b));
            }
          }
        }
      }
      std::vector<uint16_t> capture;
      bool rearranged;
      uint64_t generation;
      {
        std::lock_guard<std::mutex> lock(mutex);
        tick();
        rearranged = capture_rearranged;
        generation = capture_generation;
        auto now = Clock::now();
        size_t in_flight =
            std::count(capture_pending.begin(), capture_pending.end(), true);
        for (uint16_t n = 0; n < 4 && in_flight < capture_max_inflight; ++n) {
          uint16_t d = uint16_t((capture_cursor - 1 + n) % 4 + 1);
          if ((capture_mask & (1 << (d - 1))) && !capture_pending[d] &&
              now >= next_capture[d]) {
            capture.push_back(d);
            ++in_flight;
          }
        }
        if (!capture.empty())
          capture_cursor = uint16_t(capture.back() % 4 + 1);
      }
      for (auto d : capture)
        submit(d, 0x0300, 0x80, {0, uint8_t(rearranged ? 0 : 1)},
               {std::chrono::milliseconds{1000}, 0}, true, generation);
    }
  }
  void decode_loop() {
    for (;;) {
      Response r;
      std::function<void(const Response &)> resp;
      std::function<void(std::shared_ptr<const CameraEvent>)> cam;
      std::function<void(const BatchEvent &)> batch;
      std::function<void(const std::string &)> error;
      std::pair<uint32_t, uint32_t> dim;
      ImageLayout image_layout;
      {
        std::unique_lock<std::mutex> lock(mutex);
        events_ready.wait(lock, [&] { return stopping || !events.empty(); });
        if (stopping)
          break;
        queued_bytes -= events.front().frame.data.size();
        r = std::move(events.front());
        events.pop_front();
        resp = response_callback;
        cam = camera_callback;
        batch = batch_callback;
        error = error_callback;
        dim = dimensions[std::min<uint16_t>(r.frame.device, 4)];
        image_layout = image_layouts[std::min<uint16_t>(r.frame.device, 4)];
      }
      try {
        auto cmd = r.frame.command;
        if (r && r.frame.rw == 0x80 && r.frame.device >= 1 &&
            r.frame.device <= 4 && cmd >= 0x0300 && cmd <= 0x0304 &&
            r.frame.data.size() > 2) {
          auto event = std::make_shared<CameraEvent>();
          event->device = r.frame.device;
          event->command = cmd;
          event->message_id = r.frame.message_id;
          if (cmd >= 0x0302)
            event->data = decode_numeric_response(cmd, r.frame.data);
          else {
            try {
              event->data =
                  decode_camera_data(r.frame.data, cmd == 0x0300, image_layout);
            } catch (const std::exception &) {
              // Legacy image format has a fixed-size pixel plane and no block
              // tags.
              size_t pixels = size_t(dim.first) * dim.second;
              if (r.frame.data.size() != pixels &&
                  r.frame.data.size() != pixels + 2)
                throw;
              event->data =
                  decode_legacy_image(r.frame.data, dim.first, dim.second);
            }
          }
          {
            std::lock_guard<std::mutex> lock(mutex);
            event->sequence = ++sequence[event->device];
            latest[event->device] = event;
            if (event->data.image)
              ++stats.decoded_images;
            frame_ready.notify_all();
          }
          if (cam)
            cam(event);
        } else if (r && r.frame.rw == 0x80 &&
                   (cmd == 0x0204 || cmd == 0x0205) &&
                   r.frame.data.size() > 2) {
          BatchEvent event;
          event.device = r.frame.device;
          event.message_id = r.frame.message_id;
          event.command = cmd;
          event.data = cmd == 0x0204 ? decode_realtime_batch(r.frame.data)
                                     : decode_all_batch(r.frame.data);
          if (batch)
            batch(event);
        }
        if (resp)
          resp(r);
        if (!r && error)
          error(r.error);
      } catch (const std::exception &e) {
        if (error) {
          try {
            error(std::string("Decode/callback error: ") + e.what());
          } catch (...) {
          }
        }
      } catch (...) {
        if (error) {
          try {
            error("Callback threw an unknown exception");
          } catch (...) {
          }
        }
      }
    }
  }
};
Device::Device() : impl_(new Impl) {}
Device::~Device() { close(); }
void Device::open(const ConnectionOptions &o) {
  auto &i = *impl_;
  std::lock_guard<std::mutex> life(i.lifecycle);
  std::unique_lock<std::mutex> lock(i.mutex);
  if (i.opened)
    throw std::logic_error("Device is already open");
  if (!o.device_port || o.max_frame_bytes < 18 ||
      o.max_frame_bytes > 64 * 1024 * 1024 ||
      o.max_assembly_bytes < o.max_frame_bytes || !o.decode_queue_size ||
      !o.max_pending_requests || o.max_pending_bytes < 64 ||
      !o.max_decode_bytes || o.assembly_timeout.count() <= 0)
    throw std::invalid_argument("Invalid connection limits");
  auto target = net::address(o.device_ip, o.device_port);
  auto socket = net::bind(o.local_ip, o.local_port, o.receive_buffer);
  auto data = net::invalid;
  try {
    net::nonblocking(socket);
    if (o.data_port && o.data_port != net::port(socket))
      data = net::bind(o.local_ip, o.data_port, o.receive_buffer);
    if (data != net::invalid)
      net::nonblocking(data);
  } catch (...) {
    net::close(socket);
    net::close(data);
    throw;
  }
  i.options = o;
  i.target = target;
  i.socket = socket;
  i.data_socket = data;
  i.bound_port = net::port(socket);
  i.stopping = false;
  i.opened = true;
  i.stats = {};
  i.capture_mask = 0;
  i.capture_pending = {};
  i.sequence = {};
  i.latest = {};
  i.retired = {};
  i.assemblies.clear();
  i.completed.clear();
  i.events.clear();
  i.queued_bytes = 0;
  try {
    i.receiver = std::thread([&i] { i.receive_loop(); });
    i.decoder = std::thread([&i] { i.decode_loop(); });
    i.receiver_id = i.receiver.get_id();
    i.decoder_id = i.decoder.get_id();
  } catch (...) {
    i.stopping = true;
    i.events_ready.notify_all();
    lock.unlock();
    if (i.receiver.joinable())
      i.receiver.join();
    if (i.decoder.joinable())
      i.decoder.join();
    lock.lock();
    net::close(i.socket);
    net::close(i.data_socket);
    i.socket = net::invalid;
    i.data_socket = net::invalid;
    i.opened = false;
    i.receiver_id = {};
    i.decoder_id = {};
    throw;
  }
}
void Device::close() {
  auto &i = *impl_;
  {
    std::lock_guard<std::mutex> lock(i.mutex);
    if (std::this_thread::get_id() == i.decoder_id ||
        std::this_thread::get_id() == i.receiver_id)
      throw std::logic_error(
          "Close Device from an application thread, outside callbacks");
  }
  std::lock_guard<std::mutex> life(i.lifecycle);
  {
    std::lock_guard<std::mutex> lock(i.mutex);
    if (!i.opened)
      return;
    i.stopping = true;
    i.capture_mask = 0;
    while (!i.pending.empty())
      i.finish(i.pending.begin(), failure(Status::Cancelled, "Device closed",
                                          i.pending.begin()->second->frame));
    i.events_ready.notify_all();
    i.frame_ready.notify_all();
  }
  if (i.receiver.joinable())
    i.receiver.join();
  if (i.decoder.joinable())
    i.decoder.join();
  {
    std::lock_guard<std::mutex> lock(i.mutex);
    net::close(i.socket);
    net::close(i.data_socket);
    i.socket = net::invalid;
    i.data_socket = net::invalid;
    i.opened = false;
    i.receiver_id = {};
    i.decoder_id = {};
    i.events.clear();
    i.assemblies.clear();
    i.stats.assembly_bytes = 0;
  }
}
bool Device::is_open() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->opened && !impl_->stopping;
}
uint16_t Device::local_port() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->bound_port;
}
std::future<Response> Device::request_async(uint16_t d, uint16_t c, uint8_t rw,
                                            Bytes data, RequestOptions o) {
  return impl_->submit(d, c, rw, std::move(data), o);
}
Response Device::request(uint16_t d, uint16_t c, uint8_t rw, const Bytes &data,
                         RequestOptions o) {
  return request_async(d, c, rw, data, o).get();
}
Response Device::read(uint16_t d, uint16_t c, RequestOptions o) {
  return request(d, c, 0x80, read_payload(c), o);
}
Response Device::write(uint16_t d, uint16_t c, const Bytes &data,
                       RequestOptions o) {
  validate_write(c, data);
  return request(d, c, 0x40, data, o);
}
Response Device::write_u32(uint16_t d, uint16_t c, uint32_t v,
                           RequestOptions o) {
  return write(d, c, u32(v), o);
}
Response Device::set_sample_rate(double hz) {
  return write_u32(0, 0x000a, sample_rate_to_ticks(hz));
}
Response Device::set_exposure_delay(const std::array<uint32_t, 4> &ticks) {
  Bytes b;
  for (auto n : ticks) {
    auto v = u32(n);
    b.insert(b.end(), v.begin(), v.end());
  }
  return write(0, 6, b);
}
Response Device::set_camera_power(uint16_t d, bool on) {
  return write_u32(d, 0x000b, on ? 0 : 1);
}
Response Device::set_roi(uint16_t d, bool x, uint16_t start, uint16_t count) {
  return write_u32(d, x ? 3 : 4, (uint32_t(start) << 16) | count);
}
Response Device::set_batch_length(uint32_t n,
                                  const std::vector<uint16_t> &devices) {
  Response r;
  for (auto d : devices) {
    r = write_u32(d, 0x0201, n);
    if (!r)
      break;
  }
  return r;
}
Response Device::set_batch_control(bool start, uint16_t index,
                                   const std::vector<uint16_t> &devices) {
  Response r;
  for (auto d : devices) {
    r = write_u32(d, 0x0203, (uint32_t(start ? 1 : 0) << 16) | index);
    if (!r)
      break;
  }
  return r;
}
Response Device::reset_buffer(uint16_t d) {
  return request(d, 0x0206, 0x40, {0, 1});
}
Response Device::get_batch_data(uint16_t d, bool all) {
  return read(d, all ? 0x0205 : 0x0204);
}
Response Device::get_camera_data(uint16_t d, bool rearranged,
                                 RequestOptions o) {
  return request(d, 0x0300, 0x80, {0, uint8_t(rearranged ? 0 : 1)}, o);
}
void Device::heartbeat() {
  auto &i = *impl_;
  std::lock_guard<std::mutex> lock(i.mutex);
  if (!i.opened || i.stopping)
    return;
  CommandFrame f{0, 0x0401, i.allocate_id(), 0, 0x80, {0, 0}};
  i.send_packets(split_transport(encode_command(f), 0, f.message_id));
}
void Device::start_capture(uint8_t mask, bool rearranged,
                           std::chrono::milliseconds interval) {
  if (!mask || (mask & 0xf0) || interval.count() < 0)
    throw std::invalid_argument("Invalid capture mask or interval");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->opened || impl_->stopping)
    throw std::logic_error("Device is not open");
  ++impl_->capture_generation;
  impl_->capture_mask = mask;
  impl_->capture_rearranged = rearranged;
  impl_->capture_interval = interval;
  impl_->capture_cursor = 1;
  impl_->next_capture = {};
}
void Device::set_capture_concurrency(size_t max_inflight) {
  if (max_inflight < 1 || max_inflight > 4)
    throw std::invalid_argument("Capture concurrency must be in 1..4");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->capture_max_inflight = max_inflight;
}
void Device::stop_capture() {
  auto &i = *impl_;
  std::lock_guard<std::mutex> lock(i.mutex);
  ++i.capture_generation;
  i.capture_mask = 0;
  for (auto it = i.pending.begin(); it != i.pending.end();) {
    auto current = it++;
    if (current->second->capture)
      i.finish(current, failure(Status::Cancelled, "Capture stopped",
                                current->second->frame));
  }
}
void Device::set_image_dimensions(uint16_t d, uint32_t w, uint32_t h) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (d < 1 || d > 4 || !w || !h || h % 2 ||
      uint64_t(w) * h > impl_->options.max_frame_bytes)
    throw std::invalid_argument("Invalid image dimensions");
  impl_->dimensions[d] = {w, h};
}
void Device::set_image_layout(uint16_t d, ImageLayout layout) {
  if (d < 1 || d > 4 ||
      (layout != ImageLayout::Auto && layout != ImageLayout::Declared &&
       layout != ImageLayout::FpgaRaw && layout != ImageLayout::Standard))
    throw std::invalid_argument("Invalid sensor or image layout");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->image_layouts[d] = layout;
}
std::shared_ptr<const CameraEvent> Device::latest_frame(uint16_t d) const {
  if (d < 1 || d > 4)
    throw std::invalid_argument("Sensor must be 1..4");
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->latest[d];
}
std::shared_ptr<const CameraEvent>
Device::wait_next_frame(uint16_t d, uint64_t seq,
                        std::chrono::milliseconds timeout) {
  if (d < 1 || d > 4)
    throw std::invalid_argument("Sensor must be 1..4");
  auto &i = *impl_;
  std::unique_lock<std::mutex> lock(i.mutex);
  if (std::this_thread::get_id() == i.decoder_id)
    throw std::logic_error(
        "Wait for frames from an application thread, outside callbacks");
  i.frame_ready.wait_for(lock, timeout, [&] {
    return !i.opened || i.stopping ||
           (i.latest[d] && i.latest[d]->sequence > seq);
  });
  return i.opened && !i.stopping && i.latest[d] && i.latest[d]->sequence > seq
             ? i.latest[d]
             : nullptr;
}
void Device::on_response(std::function<void(const Response &)> f) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->response_callback = std::move(f);
}
void Device::on_camera(
    std::function<void(std::shared_ptr<const CameraEvent>)> f) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->camera_callback = std::move(f);
}
void Device::on_batch(std::function<void(const BatchEvent &)> f) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->batch_callback = std::move(f);
}
void Device::on_error(std::function<void(const std::string &)> f) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->error_callback = std::move(f);
}
Statistics Device::statistics() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto s = impl_->stats;
  s.pending_requests = impl_->pending.size();
  s.queued_events = impl_->events.size();
  return s;
}
} // namespace fpga
