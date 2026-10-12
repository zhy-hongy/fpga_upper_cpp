#include "fpga/upgrade.hpp"
#include "fpga/wire.hpp"
#include "socket.hpp"
#include <algorithm>
#include <limits>
#include <thread>

namespace fpga {
std::vector<UpgradeResult>
upgrade_firmware(std::vector<FirmwareEntry> targets, const UpgradeOptions &o,
                 std::function<void(const UpgradeProgress &)> progress,
                 const std::atomic<bool> *cancelled) {
  if (targets.empty() || o.timeout.count() <= 0 ||
      o.request_interval.count() <= 0 || o.request_retries > 100 ||
      o.send_interval.count() < 0 || o.inter_device_delay.count() < 0)
    throw std::invalid_argument("Invalid upgrade options");
  std::stable_sort(targets.begin(), targets.end(),
                   [](const auto &a, const auto &b) {
                     return a.chip_id == 0 && b.chip_id != 0;
                   });
  struct Prepared {
    FirmwareEntry entry;
    FirmwareChunks firmware;
  };
  std::vector<Prepared> prepared;
  size_t total = 0;
  std::array<bool, 5> chips{};
  for (auto &t : targets) {
    if (t.chip_id > 4 || chips[t.chip_id] || t.data.empty() ||
        t.data.size() > std::numeric_limits<uint32_t>::max() - 4 ||
        uint64_t(t.flash_addr) + t.data.size() + 4 > 0x100000000ull)
      throw std::invalid_argument("Invalid upgrade target or flash range");
    chips[t.chip_id] = true;
    auto f = split_firmware_bytes(t.data);
    if (f.chunks.size() > 65534)
      throw std::invalid_argument("Firmware exceeds 16-bit sequence range");
    total += f.chunks.size();
    prepared.push_back({std::move(t), std::move(f)});
  }
  struct Owner {
    net::Socket s = net::invalid;
    ~Owner() { net::close(s); }
  } socket;
  auto peer = net::address(o.device_ip, o.device_port);
  socket.s = net::bind(o.local_ip, o.local_port);
  auto abort = [&] { return cancelled && cancelled->load(); };
  auto pause = [&](std::chrono::milliseconds duration) {
    auto end = std::chrono::steady_clock::now() + duration;
    while (!abort() && std::chrono::steady_clock::now() < end)
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
  };
  // Preserve a result arriving alongside the final data ACK.
  std::vector<UpgradeFrame> inbox;
  auto receive = [&](uint8_t chip, uint8_t function, uint16_t sequence,
                     std::chrono::milliseconds timeout, UpgradeFrame &answer,
                     bool final_data = false) {
    auto matching = [&](const UpgradeFrame &r) {
      return !r.down && r.chip_id == chip && r.function == function &&
             (r.sequence == sequence ||
              (function == 0x83 && r.sequence == uint16_t(sequence - 1)));
    };
    auto cached = std::find_if(inbox.begin(), inbox.end(), matching);
    if (cached != inbox.end()) {
      answer = *cached;
      inbox.erase(cached);
      return true;
    }
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!abort() && std::chrono::steady_clock::now() < deadline) {
      if (!net::ready(socket.s, std::chrono::milliseconds{10}))
        continue;
      sockaddr_in source{};
      auto b = net::receive(socket.s, source);
      if (!net::same_peer(peer, source))
        continue;
      try {
        auto r = decode_upgrade_frame(b);
        if (matching(r)) {
          answer = std::move(r);
          return true;
        }
        if (final_data && !r.down && r.chip_id == chip && r.function == 0x83 &&
            (r.sequence == sequence || r.sequence == uint16_t(sequence + 1))) {
          if (inbox.size() < 8)
            inbox.push_back(std::move(r));
        }
      } catch (const std::exception &) {
      }
    }
    return false;
  };
  std::vector<UpgradeResult> results;
  size_t done = 0;
  std::vector<size_t> absent;
  auto one = [&](Prepared &p, size_t base) {
    inbox.clear();
    UpgradeResult result{p.entry.chip_id, false, {}};
    uint16_t seq = 0;
    UpgradeFrame ack;
    auto chip = p.entry.chip_id;
    auto report = [&](size_t n, const std::string &stage) {
      if (progress)
        progress({chip, base + n, total, stage});
    };
    auto fail = [&](const std::string &reason) {
      result.error = abort() ? "Cancelled" : reason;
      return result;
    };
    report(0, "request");
    bool requested = false;
    for (unsigned attempt = 0; attempt <= o.request_retries && !abort();
         ++attempt) {
      if (!net::send(socket.s, peer, pack_upgrade_request(seq, chip)))
        return fail("UDP request send failed");
      if (receive(chip, 0x80, seq, o.request_interval, ack) &&
          ack.payload == Bytes{0xa5, 0xa5}) {
        requested = true;
        break;
      }
    }
    if (!requested)
      return fail("Request timeout");
    ++seq;
    report(0, "control");
    if (!net::send(socket.s, peer,
                   pack_upgrade_control(
                       seq, p.entry.flash_addr,
                       static_cast<uint32_t>(p.entry.data.size() + 4), chip)))
      return fail("UDP control send failed");
    if (!receive(chip, 0x81, seq, o.timeout, ack) ||
        ack.payload != Bytes{0xa5, 0xa5})
      return fail("Control ACK timeout or failure");
    ++seq;
    for (size_t n = 0; n < p.firmware.chunks.size(); ++n) {
      if (abort())
        return fail("Cancelled");
      if (!net::send(socket.s, peer,
                     pack_upgrade_data(seq, static_cast<uint16_t>(n),
                                       p.firmware.chunks[n], chip)))
        return fail("UDP data send failed");
      if (!receive(chip, 0x82, seq, o.timeout, ack,
                   n + 1 == p.firmware.chunks.size()) ||
          ack.payload.size() != 2 || read_u16(ack.payload) != n + 1)
        return fail("Data ACK timeout or block mismatch");
      ++seq;
      report(n + 1, "data");
      pause(o.send_interval);
    }
    report(p.firmware.chunks.size(), "result");
    if (!receive(chip, 0x83, seq, o.timeout, ack))
      return fail("Result timeout");
    if (ack.payload != Bytes{0xa5, 0xa5})
      return fail("Device reported upgrade failure");
    if (!net::send(socket.s, peer, pack_upgrade_result_ack(seq, chip)))
      return fail("Result confirmation send failed");
    result.success = true;
    report(p.firmware.chunks.size(), "complete");
    return result;
  };
  for (size_t n = 0; n < prepared.size(); ++n) {
    auto r = one(prepared[n], done);
    if (r.error == "Request timeout")
      absent.push_back(n);
    else
      done += prepared[n].firmware.chunks.size();
    results.push_back(std::move(r));
    if (abort())
      break;
    if (n + 1 < prepared.size() && results.back().success)
      pause(o.inter_device_delay);
  }
  for (auto n : absent) {
    if (abort())
      break;
    results[n] = one(prepared[n], done);
    done += prepared[n].firmware.chunks.size();
  }
  return results;
}
} // namespace fpga
