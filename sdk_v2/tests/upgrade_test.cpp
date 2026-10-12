#include "../src/socket.hpp"
#include "fpga/upgrade.hpp"
#include "fpga/wire.hpp"
#include <atomic>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
using namespace fpga;
using namespace std::chrono_literals;
namespace {
unsigned checks = 0;
void require(bool ok, const char *message) {
  ++checks;
  if (!ok)
    throw std::runtime_error(message);
}
struct UpgradePeer {
  net::Socket socket = net::bind("127.0.0.1", 0);
  std::atomic<bool> running{true};
  std::thread worker;
  std::atomic<unsigned> data_blocks{0}, request_frames{0}, confirmations{0};
  std::map<uint8_t, Bytes> received;
  std::vector<uint8_t> order;
  bool fail_data = false, ignore_request = false;
  UpgradePeer(bool fail = false, bool ignore = false)
      : fail_data(fail), ignore_request(ignore) {
    worker = std::thread([this] { loop(); });
  }
  ~UpgradePeer() {
    running = false;
    worker.join();
    net::close(socket);
  }
  uint16_t port() { return net::port(socket); }
  void loop() {
    std::map<uint8_t, uint32_t> expected;
    while (running) {
      if (!net::ready(socket, 5ms))
        continue;
      sockaddr_in peer{};
      auto b = net::receive(socket, peer);
      try {
        auto f = decode_upgrade_frame(b);
        if (!f.down)
          continue;
        auto answer = f;
        answer.down = false;
        if (f.function == 0x80) {
          ++request_frames;
          if (ignore_request)
            continue;
          order.push_back(f.chip_id);
          answer.payload = {0xa5, 0xa5};
        }
        if (f.function == 0x81) {
          expected[f.chip_id] = read_u32(f.payload, 4);
          received[f.chip_id].clear();
          answer.payload = {0xa5, 0xa5};
        }
        if (f.function == 0x82) {
          ++data_blocks;
          auto idx = read_u16(f.payload);
          received[f.chip_id].insert(received[f.chip_id].end(),
                                     f.payload.begin() + 2, f.payload.end());
          answer.payload = pack_u16(idx + (fail_data ? 2 : 1));
        }
        if (f.function == 0x83) {
          ++confirmations;
          continue;
        }
        // A forged chip response must be ignored, then the true ACK resolves.
        auto wrong = answer;
        wrong.chip_id = uint8_t((wrong.chip_id + 1) % 5);
        net::send(socket, peer, pack_upgrade_frame(wrong));
        net::send(socket, peer, pack_upgrade_frame(answer));
        if (f.function == 0x82 &&
            received[f.chip_id].size() == expected[f.chip_id]) {
          answer.function = 0x83;
          answer.payload = {0xa5, 0xa5};
          net::send(socket, peer, pack_upgrade_frame(answer));
        }
      } catch (const std::exception &) {
      }
    }
  }
};
UpgradeOptions options(UpgradePeer &p) {
  UpgradeOptions o;
  o.device_ip = "127.0.0.1";
  o.device_port = p.port();
  o.local_ip = "127.0.0.1";
  o.local_port = 0;
  o.timeout = 100ms;
  o.request_interval = 30ms;
  o.request_retries = 1;
  o.inter_device_delay = 0ms;
  return o;
}
} // namespace
int main() {
  try {
    UpgradePeer peer;
    Bytes firmware(2048);
    for (size_t n = 0; n < firmware.size(); ++n)
      firmware[n] = uint8_t(n);
    size_t progress = 0, total = 0;
    auto r = upgrade_firmware({{2, 0x1000, firmware}, {0, 0, Bytes(100, 7)}},
                              options(peer), [&](const UpgradeProgress &p) {
                                progress = p.current;
                                total = p.total;
                              });
    require(r.size() == 2 && r[0].chip_id == 0 && r[0].success && r[1].success,
            "FPGA first multi-device upgrade");
    std::this_thread::sleep_for(20ms);
    require(peer.order == std::vector<uint8_t>{0, 2}, "upgrade order");
    require(peer.data_blocks == 3, "CRC appended to final existing chunk");
    require(peer.confirmations == 2, "result confirmation");
    require(progress == total && total == 3, "aggregate progress");
    auto actual = peer.received[2];
    require(actual.size() == 2052, "firmware plus CRC length");
    require(Bytes(actual.begin(), actual.begin() + 2048) == firmware,
            "firmware bytes unchanged");
    require(read_u32(actual, 2048) == firmware_crc32(firmware),
            "final CRC32 matches");
    UpgradePeer bad(true);
    r = upgrade_firmware({{0, 0, firmware}}, options(bad));
    require(!r[0].success &&
                r[0].error.find("block mismatch") != std::string::npos,
            "bad ACK fails upgrade");
    require(bad.data_blocks == 1, "data writes never blindly retransmitted");
    UpgradePeer silent(false, true);
    r = upgrade_firmware({{0, 0, firmware}}, options(silent));
    require(!r[0].success && r[0].error == "Request timeout",
            "request timeout");
    require(silent.request_frames == 4,
            "initial request retries and deferred absent-device retry");
    std::atomic<bool> cancel{true};
    r = upgrade_firmware({{0, 0, firmware}}, options(peer), {}, &cancel);
    require(!r[0].success && r[0].error == "Cancelled", "upgrade cancellation");
    std::cout << "Upgrade: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAILED: " << e.what() << "\n";
    return 1;
  }
}
