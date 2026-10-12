#include "../src/socket.hpp"
#include "fpga/device.hpp"
#include <atomic>
#include <iostream>
#include <thread>
using namespace fpga;
using namespace std::chrono_literals;
int main() {
  net::Socket socket = net::invalid;
  std::thread sender;
  std::atomic<bool> running{false};
  try {
    const uint32_t width = 256, height = 16, lanes = 2, cycles = 128;
    Bytes expected(width * height), raw(expected.size());
    for (size_t n = 0; n < expected.size(); ++n)
      expected[n] = uint8_t((n * 17 + n / width * 23) % 251);
    for (uint32_t pair = 0; pair < height / 2; ++pair)
      for (uint32_t cycle = 0; cycle < cycles; ++cycle)
        for (uint32_t odd = 0; odd < 2; ++odd)
          for (uint32_t lane = 0; lane < lanes; ++lane)
            raw[((pair * cycles + cycle) * 2 + odd) * lanes +
                (lanes - 1 - lane)] =
                expected[(pair * 2 + odd) * width + lane * cycles + cycle];
    Bytes body;
    auto append = [&](Bytes v) { body.insert(body.end(), v.begin(), v.end()); };
    append(pack_u16(width));
    append(pack_u16(height));
    append(pack_u16(1));
    append(raw);
    Bytes payload = pack_camera_data_block(1, body, false);
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
          request.command = 0x0300;
          request.data = payload;
          for (const auto &packet : split_transport(
                   encode_command(request), request.device, request.message_id))
            net::send(socket, peer, packet);
        } catch (const std::exception &) {
        }
      }
    });
    Device device;
    ConnectionOptions options;
    options.device_ip = "127.0.0.1";
    options.device_port = net::port(socket);
    options.local_ip = "127.0.0.1";
    options.local_port = 0;
    device.open(options);
    if (!device.get_camera_data(1, true, {1s, 0}))
      throw std::runtime_error("Declared request failed");
    auto first = device.wait_next_frame(1, 0, 1s);
    if (!first || !first->data.image || first->data.image->pixels != raw)
      throw std::runtime_error("Declared mode changed source pixels");
    device.set_image_layout(1, ImageLayout::FpgaRaw);
    if (!device.get_camera_data(1, true, {1s, 0}))
      throw std::runtime_error("Override request failed");
    auto second = device.wait_next_frame(1, first->sequence, 1s);
    if (!second || !second->data.image ||
        second->data.image->pixels != expected ||
        second->data.image_layout.selected != ImageLayout::FpgaRaw ||
        second->data.notes.empty())
      throw std::runtime_error("Explicit raw override did not recover original "
                               "pixels and diagnostic");
    if (!device.get_camera_data(2, true, {1s, 0}))
      throw std::runtime_error("Second sensor request failed");
    auto other = device.wait_next_frame(2, 0, 1s);
    if (!other || !other->data.image || other->data.image->pixels != raw)
      throw std::runtime_error("Override leaked to another sensor");
    device.set_image_layout(1, ImageLayout::Standard);
    if (!device.get_camera_data(1, true, {1s, 0}))
      throw std::runtime_error("Standard request failed");
    auto third = device.wait_next_frame(1, second->sequence, 1s);
    if (!third || !third->data.image || third->data.image->pixels != raw)
      throw std::runtime_error("Standard override was not applied");
    device.close();
    running = false;
    sender.join();
    net::close(socket);
    socket = net::invalid;
    std::cout << "Layout override: exact raw pixel recovery, diagnostics, "
                 "per-sensor isolation, switching passed\n";
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
