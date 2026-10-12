#include "../src/socket.hpp"
#include "fpga/device.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace fpga;
using namespace std::chrono_literals;
int main() {
  net::Socket socket = net::invalid;
  std::atomic<bool> running{false};
  std::thread sender;
  try {
    const uint32_t width = 2048, height = 1152;
    Bytes image(width * height);
    for (uint32_t y = 0; y < height; ++y)
      for (uint32_t x = 0; x < width; ++x)
        image[y * width + x] = uint8_t((x * 7 + y * 3) % 251);
    Bytes block;
    auto append = [&](const Bytes &b) {
      block.insert(block.end(), b.begin(), b.end());
    };
    append(pack_u16(width));
    append(pack_u16(height));
    append(pack_u16(1));
    append(image);
    Bytes payload = pack_camera_data_block(1, block);
    socket = net::bind("127.0.0.1", 0);
    running = true;
    sender = std::thread([&] {
      while (running) {
        if (!net::ready(socket, 5ms))
          continue;
        sockaddr_in peer{};
        auto bytes = net::receive(socket, peer);
        try {
          auto request = decode_command(decode_transport(bytes).data);
          request.type = message_response;
          request.data = payload;
          auto packets = split_transport(encode_command(request),
                                         request.device, request.message_id);
          for (const auto &p : packets)
            net::send(socket, peer, p);
        } catch (const std::exception &) {
        }
      }
    });
    Device device;
    ConnectionOptions o;
    o.device_ip = "127.0.0.1";
    o.device_port = net::port(socket);
    o.local_ip = "127.0.0.1";
    o.local_port = 0;
    device.open(o);
    auto start = std::chrono::steady_clock::now();
    unsigned frames = 0;
    for (unsigned n = 0; n < 16; ++n) {
      uint16_t sensor = uint16_t(n % 4 + 1);
      auto last = device.latest_frame(sensor);
      uint64_t sequence = last ? last->sequence : 0;
      auto r = device.get_camera_data(sensor, true, {3000ms, 0});
      if (!r)
        throw std::runtime_error(r.error);
      auto f = device.wait_next_frame(sensor, sequence, 3000ms);
      if (!f || !f->data.image || f->data.image->pixels != image)
        throw std::runtime_error(
            "Full-resolution decoded frame differs from source");
      ++frames;
    }
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    auto stats = device.statistics();
    if (stats.malformed_packets || stats.incomplete_frames ||
        stats.decoded_images != frames || stats.pending_requests ||
        stats.assembly_bytes)
      throw std::runtime_error(
          "Full-resolution stream lost frames or retained pending state");
    device.close();
    running = false;
    sender.join();
    net::close(socket);
    socket = net::invalid;
    std::cout << frames << " full-resolution frames, 2048x1152 Gray8, "
              << stats.rx_packets << " packets, " << frames / seconds
              << " fps loopback, " << stats.rx_bytes / seconds / 1000000
              << " MB/s, zero dropped/malformed frames\n";
    return 0;
  } catch (const std::exception &e) {
    running = false;
    if (sender.joinable())
      sender.join();
    net::close(socket);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
