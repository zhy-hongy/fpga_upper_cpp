#include "fpga/device.hpp"
#include "fpga/tools.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace fpga;
using namespace std::chrono_literals;
int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: fpga_hardware_capture_soak <device-ip> <local-ip> "
                 "<new-output-dir>\n";
    return 2;
  }
  try {
    std::filesystem::path output(argv[3]);
    if (!std::filesystem::create_directory(output))
      throw std::runtime_error("Output directory already exists");
    Device device;
    ConnectionOptions options;
    options.device_ip = argv[1];
    options.local_ip = argv[2];
    device.open(options);
    std::array<std::atomic<unsigned>, 5> counts{};
    device.on_camera([&](std::shared_ptr<const CameraEvent> e) {
      if (e && e->data.image)
        ++counts[e->device];
    });
    for (uint16_t d = 1; d <= 4; ++d)
      device.set_image_layout(d, ImageLayout::FpgaRaw);
    device.set_capture_concurrency(1);
    device.start_capture(0x0f, true, 40ms);
    std::this_thread::sleep_for(5s);
    device.stop_capture();
    device.close(); // Join callbacks before taking one consistent count snapshot.
    auto stats = device.statistics();
    bool complete = true;
    std::ofstream log(output / "continuous_capture.json");
    log << "{\"heartbeat_skipped\":true,\"capture_concurrency\":1,\"seconds\":"
           "5,\"frames\":[";
    for (uint16_t d = 1; d <= 4; ++d) {
      unsigned n = counts[d];
      complete = complete && n >= 5;
      if (d > 1)
        log << ',';
      log << n;
      auto f = device.latest_frame(d);
      if (f && f->data.image)
        save_image_pgm(
            (output / ("camera" + std::to_string(d) + ".pgm")).string(),
            *f->data.image);
    }
    log << "],\"rx_packets\":" << stats.rx_packets
        << ",\"rx_bytes\":" << stats.rx_bytes
        << ",\"malformed\":" << stats.malformed_packets
        << ",\"incomplete\":" << stats.incomplete_frames
        << ",\"timeouts\":" << stats.request_timeouts
        << ",\"dropped_events\":" << stats.dropped_events
        << ",\"images\":" << stats.decoded_images << "}\n";
    std::cout << "Continuous serial hardware capture: ";
    for (uint16_t d = 1; d <= 4; ++d)
      std::cout << "CAM" << d << '=' << counts[d] << ' ';
    std::cout << " rx=" << stats.rx_packets
              << " malformed=" << stats.malformed_packets
              << " incomplete=" << stats.incomplete_frames
              << " timeouts=" << stats.request_timeouts
              << " dropped=" << stats.dropped_events << std::endl;
    device.close();
    return complete && !stats.malformed_packets && !stats.incomplete_frames &&
                   !stats.request_timeouts && !stats.dropped_events
               ? 0
               : 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 2;
  }
}
