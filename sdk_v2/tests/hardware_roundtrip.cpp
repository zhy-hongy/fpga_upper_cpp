#include "fpga/device.hpp"
#include "fpga/tools.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace fpga;
using namespace std::chrono_literals;
namespace {
std::string hex(const Bytes &b) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (auto value : b)
    out << std::setw(2) << unsigned(value);
  return out.str();
}
std::string quoted(const std::string &value) {
  std::string out = "\"";
  for (unsigned char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += char(c);
    }
  }
  return out + '"';
}
const char *status_name(Status s) {
  switch (s) {
  case Status::Ok:
    return "ok";
  case Status::Timeout:
    return "timeout";
  case Status::Cancelled:
    return "cancelled";
  case Status::DeviceError:
    return "device_error";
  case Status::ProtocolError:
    return "protocol_error";
  default:
    return "network_error";
  }
}
struct RegisterSpec {
  uint16_t command;
  const char *name;
  uint32_t minimum, maximum;
};
const RegisterSpec registers[] = {{1, "exposure_us", 0, 10000000},
                                  {2, "gain", 0, 20},
                                  {0x0d, "line_time", 128, 65535},
                                  {0x11, "exposure_wait", 0, 65535}};
struct RegisterResult {
  uint16_t device = 0, command = 0;
  std::string name, state = "not_run", error;
  Bytes original, test, readback, restored;
  bool write_attempted = false, write_ack = false, readback_ok = false,
       restore_ack = false, restore_ok = false;
};
struct ImageResult {
  uint16_t device = 0, command = 0, message_id = 0;
  unsigned index = 0;
  bool rearranged = false, success = false, captured = false,
       command_matches = false;
  uint32_t width = 0, height = 0;
  size_t payload_bytes = 0, center = 0, cloud = 0, interp = 0;
  double latency_ms = 0;
  std::string error, stem;
  std::vector<std::string> notes;
};
} // namespace
int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: fpga_hardware_roundtrip <device-ip> <local-ip> "
                 "<new-output-dir> [--compat-crc] [--quick] [--images-only] "
                 "[--force-raw]\n       "
                 "fpga_hardware_roundtrip --simulate <new-output-dir>\n";
    return 2;
  }
  try {
    bool simulated = std::string(argv[1]) == "--simulate", compat = false,
         quick = false, images_only = false, force_raw = false;
    if (!simulated && argc < 4)
      return 2;
    for (int n = 4; n < argc; ++n) {
      compat |= std::string(argv[n]) == "--compat-crc";
      quick |= std::string(argv[n]) == "--quick";
      images_only |= std::string(argv[n]) == "--images-only";
      force_raw |= std::string(argv[n]) == "--force-raw";
    }
    std::filesystem::path output(simulated ? argv[2] : argv[3]);
    if (!std::filesystem::create_directory(output))
      throw std::runtime_error("Output directory already exists");
    std::ofstream log(output / "transactions.tsv"),
        originals(output / "original_registers.tsv"),
        errors(output / "decode_errors.log");
    if (!log || !originals || !errors)
      throw std::runtime_error("Cannot create evidence files");
    log << "device\tcommand\toperation\tmsg_id\tstatus\trequest_hex\treply_hex_"
           "or_bytes\terror\n";
    originals << "device\tcommand\tname\toriginal_hex\n";
    std::mutex error_mutex;
    Simulator simulator;
    Device device;
    ConnectionOptions options;
    options.verify_crc = !compat;
    if (simulated) {
      simulator.start();
      options.device_ip = "127.0.0.1";
      options.device_port = simulator.port();
      options.local_ip = "127.0.0.1";
      options.local_port = 0;
    } else {
      options.device_ip = argv[1];
      options.local_ip = argv[2];
    }
    device.on_error([&](const std::string &error) {
      std::lock_guard<std::mutex> lock(error_mutex);
      errors << error << '\n';
      errors.flush();
    });
    device.open(options);
    if (force_raw)
      for (uint16_t sensor = 1; sensor <= 4; ++sensor)
        device.set_image_layout(sensor, ImageLayout::FpgaRaw);
    std::cout << "DIRECT CAMERA TEST; heartbeat skipped; " << options.local_ip
              << ':' << device.local_port() << " -> " << options.device_ip
              << ':' << options.device_port
              << "; CRC=" << (compat ? "compat-disabled" : "strict")
              << std::endl;
    auto request = [&](uint16_t sensor, uint16_t command, uint8_t rw,
                       const Bytes &data, const std::string &stage,
                       RequestOptions timing) {
      auto response = device.request(sensor, command, rw, data, timing);
      log << sensor << '\t' << std::hex << command << std::dec << '\t' << stage
          << '\t' << response.frame.message_id << '\t'
          << status_name(response.status) << '\t' << hex(data) << '\t';
      if (response.frame.data.size() <= 512)
        log << hex(response.frame.data);
      else
        log << response.frame.data.size() << " bytes";
      log << '\t' << response.error << '\n';
      log.flush();
      return response;
    };
    std::vector<RegisterResult> register_results;
    std::vector<ImageResult> image_results;
    bool unrestored = false;
    if (!images_only)
      for (uint16_t sensor = 1; sensor <= 4; ++sensor) {
        for (const auto &spec : registers) {
          RegisterResult result;
          result.device = sensor;
          result.command = spec.command;
          result.name = spec.name;
          auto original = request(sensor, spec.command, rw_read, {0, 0},
                                  "read_original", {1500ms, 1});
          if (!original || original.frame.data.size() != 4) {
            result.state = "read_failed";
            result.error = original.error.empty()
                               ? "Expected a complete uint32 register value"
                               : original.error;
            register_results.push_back(result);
            std::cout << "CAM " << sensor << ' ' << spec.name
                      << ": READ FAILED " << result.error << std::endl;
            continue;
          }
          result.original = original.frame.data;
          uint32_t value = read_u32(result.original);
          originals << sensor << '\t' << std::hex << spec.command << std::dec
                    << '\t' << spec.name << '\t' << hex(result.original)
                    << '\n';
          originals.flush();
          if (!originals)
            throw std::runtime_error(
                "Cannot persist original value before write");
          if (value < spec.minimum || value > spec.maximum || unrestored) {
            result.state = "skipped";
            result.error = unrestored
                               ? "An earlier register restore failed; further "
                                 "writes stopped"
                               : "Original value outside known safe test range";
            register_results.push_back(result);
            std::cout << "CAM " << sensor << ' ' << spec.name << ": SKIPPED "
                      << result.error << std::endl;
            continue;
          }
          uint32_t test_value = value < spec.maximum ? value + 1 : value - 1;
          result.test = pack_u32(test_value);
          result.write_attempted = true;
          try {
            auto ack = request(sensor, spec.command, rw_write, result.test,
                               "write_test", {1500ms, 0});
            result.write_ack = bool(ack) && ack.frame.data == Bytes{0, 0};
            if (result.write_ack) {
              auto readback = request(sensor, spec.command, rw_read, {0, 0},
                                      "read_test", {1500ms, 1});
              if (readback)
                result.readback = readback.frame.data;
              result.readback_ok =
                  bool(readback) && result.readback == result.test;
              if (!result.readback_ok)
                result.error = readback.error.empty()
                                   ? "Test value readback mismatch"
                                   : readback.error;
            } else
              result.error =
                  ack.error.empty() ? "Write ACK was not 0000" : ack.error;
          } catch (const std::exception &e) {
            result.error = e.what();
          }
          // A timed-out write may have been accepted. Restore after every
          // attempt, even when its ACK was missing, and confirm the original
          // bytes again.
          auto restore =
              request(sensor, spec.command, rw_write, result.original,
                      "restore_original", {1500ms, 2});
          result.restore_ack =
              bool(restore) && restore.frame.data == Bytes{0, 0};
          auto confirm = request(sensor, spec.command, rw_read, {0, 0},
                                 "verify_restore", {1500ms, 2});
          if (confirm)
            result.restored = confirm.frame.data;
          result.restore_ok =
              bool(confirm) && result.restored == result.original;
          if (!result.restore_ok) {
            unrestored = true;
            result.error += "; ORIGINAL VALUE NOT CONFIRMED RESTORED";
          }
          result.state = result.write_ack && result.readback_ok &&
                                 result.restore_ack && result.restore_ok
                             ? "passed"
                             : "failed";
          std::cout << "CAM " << sensor << ' ' << spec.name << ": "
                    << result.state << " original=" << value
                    << " test=" << test_value
                    << " restored=" << (result.restore_ok ? "yes" : "NO") << ' '
                    << result.error << std::endl;
          register_results.push_back(std::move(result));
        }
      }
    for (uint16_t sensor = 1; sensor <= 4; ++sensor) {
      for (bool rearranged : {true, false})
        for (unsigned index = 0; index < (quick ? 1u : 3u); ++index) {
          ImageResult result;
          result.device = sensor;
          result.rearranged = rearranged;
          result.index = index;
          auto previous = device.latest_frame(sensor);
          uint64_t sequence = previous ? previous->sequence : 0;
          auto began = std::chrono::steady_clock::now();
          auto response = request(
              sensor, 0x0300, rw_read, camera_data_request_payload(rearranged),
              rearranged ? "0300_rearranged" : "0300_raw", {8000ms, 0});
          result.command = response.frame.command;
          result.message_id = response.frame.message_id;
          result.payload_bytes = response.frame.data.size();
          result.command_matches =
              result.command == (rearranged ? 0x0300 : 0x0301);
          result.stem = "camera" + std::to_string(sensor) +
                        (rearranged ? "_rearranged_" : "_raw_") +
                        std::to_string(index + 1);
          if (response && response.frame.data.size() > 2) {
            save_binary_file((output / (result.stem + ".bin")).string(),
                             encode_command(response.frame));
            std::shared_ptr<const CameraEvent> frame;
            auto decode_deadline = std::chrono::steady_clock::now() + 2s;
            while (std::chrono::steady_clock::now() < decode_deadline) {
              frame = device.wait_next_frame(sensor, sequence, 200ms);
              if (!frame)
                continue;
              sequence = frame->sequence;
              if (frame->message_id == response.frame.message_id)
                break;
              frame.reset();
            }
            if (frame && frame->data.image) {
              const auto &image = *frame->data.image;
              result.width = image.width;
              result.height = image.height;
              result.notes = frame->data.notes;
              result.center = frame->data.centerline.size();
              result.cloud = frame->data.points3d.size();
              result.interp = frame->data.interpolated.size();
              save_image_pgm((output / (result.stem + ".pgm")).string(), image);
              result.captured = true;
              result.success = result.command_matches;
              if (!result.command_matches)
                result.error =
                    "Reply command does not match requested image layout";
            } else
              result.error = "Matched raw response was saved, but no decoded "
                             "image arrived";
          } else
            result.error = response.error.empty()
                               ? "No image payload in response"
                               : response.error;
          result.latency_ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - began)
                                  .count();
          std::cout << "CAM " << sensor << " 0300 "
                    << (rearranged ? "0000" : "0001") << " #" << index + 1
                    << ": " << (result.success ? "PASS" : "FAIL")
                    << " reply=" << std::hex << result.command << std::dec
                    << ' ' << result.width << 'x' << result.height
                    << " bytes=" << result.payload_bytes
                    << " ms=" << result.latency_ms << ' ' << result.error
                    << std::endl;
          image_results.push_back(std::move(result));
          // If a layout request is unsupported, retain its first failure
          // instead of sending identical 8-second requests two more times to
          // that camera.
          if (!image_results.back().success)
            break;
        }
    }
    auto stats = device.statistics();
    device.close();
    simulator.stop();
    size_t reg_pass = 0, image_pass = 0, capture_pass = 0;
    std::ofstream summary(output / "summary.json");
    summary << "{\n\"simulated\":" << (simulated ? "true" : "false")
            << ",\"heartbeat_skipped\":true,\"crc_verified\":"
            << (compat ? "false" : "true")
            << ",\"target\":" << quoted(options.device_ip)
            << ",\"local\":" << quoted(options.local_ip)
            << ",\"image_layout_override\":"
            << quoted(force_raw ? "fpga_raw" : "declared")
            << ",\"unrestored_register\":" << (unrestored ? "true" : "false")
            << ",\n\"registers\":[\n";
    for (size_t n = 0; n < register_results.size(); ++n) {
      const auto &r = register_results[n];
      if (r.state == "passed")
        ++reg_pass;
      summary << (n ? ",\n" : "") << "{\"device\":" << r.device
              << ",\"command\":" << r.command << ",\"name\":" << quoted(r.name)
              << ",\"state\":" << quoted(r.state)
              << ",\"original\":" << quoted(hex(r.original))
              << ",\"test\":" << quoted(hex(r.test))
              << ",\"readback\":" << quoted(hex(r.readback))
              << ",\"restored\":" << quoted(hex(r.restored))
              << ",\"write_ack\":" << (r.write_ack ? "true" : "false")
              << ",\"restore_ack\":" << (r.restore_ack ? "true" : "false")
              << ",\"restore_ok\":" << (r.restore_ok ? "true" : "false")
              << ",\"error\":" << quoted(r.error) << '}';
    }
    summary << "\n],\"images\":[\n";
    for (size_t n = 0; n < image_results.size(); ++n) {
      const auto &r = image_results[n];
      image_pass += r.success;
      capture_pass += r.captured;
      summary << (n ? ",\n" : "") << "{\"device\":" << r.device
              << ",\"rearranged\":" << (r.rearranged ? "true" : "false")
              << ",\"index\":" << r.index + 1
              << ",\"success\":" << (r.success ? "true" : "false")
              << ",\"captured\":" << (r.captured ? "true" : "false")
              << ",\"command\":" << r.command
              << ",\"message_id\":" << r.message_id << ",\"width\":" << r.width
              << ",\"height\":" << r.height
              << ",\"payload_bytes\":" << r.payload_bytes
              << ",\"center\":" << r.center << ",\"cloud\":" << r.cloud
              << ",\"interpolation\":" << r.interp
              << ",\"latency_ms\":" << r.latency_ms
              << ",\"file_stem\":" << quoted(r.stem)
              << ",\"error\":" << quoted(r.error) << ",\"notes\":[";
      for (size_t k = 0; k < r.notes.size(); ++k)
        summary << (k ? "," : "") << quoted(r.notes[k]);
      summary << "]}";
    }
    summary << "\n],\"statistics\":{\"tx_packets\":" << stats.tx_packets
            << ",\"rx_packets\":" << stats.rx_packets
            << ",\"malformed\":" << stats.malformed_packets
            << ",\"incomplete\":" << stats.incomplete_frames
            << ",\"images\":" << stats.decoded_images
            << ",\"timeouts\":" << stats.request_timeouts
            << ",\"unmatched\":" << stats.unmatched_responses
            << "},\"register_pass\":" << reg_pass
            << ",\"image_pass\":" << image_pass
            << ",\"image_capture_pass\":" << capture_pass << "}\n";
    std::cout << "SUMMARY registers=" << reg_pass << '/'
              << register_results.size() << " images=" << image_pass << '/'
              << image_results.size() << " tx=" << stats.tx_packets
              << " captured=" << capture_pass << '/' << image_results.size()
              << " rx=" << stats.rx_packets
              << " malformed=" << stats.malformed_packets << " restore="
              << (unrestored ? "FAILED" : "confirmed where written")
              << "\nOUTPUT=" << std::filesystem::absolute(output).string()
              << std::endl;
    return reg_pass == register_results.size() &&
                   image_pass == image_results.size() && !unrestored
               ? 0
               : 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 2;
  }
}
