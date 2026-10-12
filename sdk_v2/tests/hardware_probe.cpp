#include "fpga/device.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace fpga;
std::string hex(const Bytes &b) {
  std::ostringstream s;
  s << std::hex << std::setfill('0');
  for (auto n : b)
    s << std::setw(2) << unsigned(n);
  return s.str();
}
int main(int argc, char **argv) {
  if (argc < 4) {
    std::cerr << "Usage: fpga_hardware_probe <device-ip> <local-ip> "
                 "<new-output-directory> [--image]\n";
    return 2;
  }
  try {
    std::filesystem::path out(argv[3]);
    if (!std::filesystem::create_directory(out))
      throw std::runtime_error("Output directory already exists");
    std::ofstream log(out / "hardware_probe.tsv");
    log << "device\tcommand\tstatus\tdata\terror\n";
    Device device;
    ConnectionOptions options;
    options.device_ip = argv[1];
    options.local_ip = argv[2];
    device.open(options);
    bool heartbeat_ok = false;
    std::array<bool, 5> sensors{};
    auto probe = [&](uint16_t d, uint16_t c, const Bytes &payload) {
      auto r = device.request(d, c, rw_read, payload,
                              {std::chrono::milliseconds{1000}, 0});
      log << d << '\t' << std::hex << c << std::dec << '\t' << int(r.status)
          << '\t' << hex(r.frame.data) << '\t' << r.error << '\n';
      log.flush();
      std::cout << "Device " << d << " cmd 0x" << std::hex << c << std::dec
                << ": " << (r ? "OK" : r.error) << " data=" << hex(r.frame.data)
                << '\n';
      return r;
    };
    heartbeat_ok = bool(probe(0, 0x0401, {0, 0}));
    if (!heartbeat_ok) {
      device.close();
      return 1;
    }
    probe(0, 0x0502, {0, 0});
    probe(0, 0x0507, {0, 4});
    probe(0, 6, {0, 0});
    probe(0, 10, {0, 0});
    probe(0, 0x0501, {0, 0});
    for (uint16_t d = 1; d <= 4; ++d) {
      auto r = probe(d, 1, {0, 0});
      sensors[d] = bool(r);
      if (!r)
        continue;
      probe(d, 2, {0, 0});
      probe(d, 7, {0, 1});
      probe(d, 0x0207, {0, 0});
    }
    if (argc > 4 && std::string(argv[4]) == "--image") {
      for (uint16_t d = 1; d <= 4; ++d)
        if (sensors[d]) {
          auto response = device.get_camera_data(
              d, true, {std::chrono::milliseconds{4000}, 0});
          log << d << "\t0300_image\t" << int(response.status) << "\t"
              << response.frame.data.size() << " bytes\t" << response.error
              << '\n';
          if (response) {
            CommandFrame raw = response.frame;
            save_binary_file(
                (out / ("camera" + std::to_string(d) + ".bin")).string(),
                encode_command(raw));
            auto frame =
                device.wait_next_frame(d, 0, std::chrono::milliseconds{4000});
            if (frame && frame->data.image) {
              auto &im = *frame->data.image;
              std::ofstream pgm(out / ("camera" + std::to_string(d) + ".pgm"),
                                std::ios::binary);
              pgm << "P5\n" << im.width << ' ' << im.height << "\n255\n";
              pgm.write(reinterpret_cast<const char *>(im.pixels.data()),
                        im.pixels.size());
              std::cout << "Decoded sensor " << d << ": " << im.width << 'x'
                        << im.height << " center "
                        << frame->data.centerline.size() << " cloud "
                        << frame->data.points3d.size() << " interp "
                        << frame->data.interpolated.size() << '\n';
            }
          }
        }
    }
    auto stats = device.statistics();
    log << "stats\ttx=" << stats.tx_packets << "\trx=" << stats.rx_packets
        << "\tmalformed=" << stats.malformed_packets
        << "\timages=" << stats.decoded_images << '\n';
    device.close();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
