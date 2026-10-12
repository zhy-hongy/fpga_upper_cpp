#pragma once
#include "fpga/data.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

namespace fpga {
struct UpgradeOptions {
    std::string device_ip="192.168.10.10",local_ip="0.0.0.0";
    uint16_t device_port=8888,local_port=8888;
    std::chrono::milliseconds timeout{5000},request_interval{5000};
    unsigned request_retries=3;
    std::chrono::milliseconds send_interval{0},inter_device_delay{3000};
};
struct UpgradeResult {uint8_t chip_id=0;bool success=false;std::string error;};
struct UpgradeProgress {uint8_t chip_id=0;size_t current=0,total=0;std::string stage;};
// Blocking upgrade worker: invoke off the GUI thread. Cancellation is observed
// during receive waits and inter-device delays. Only 0x80 requests are retried.
std::vector<UpgradeResult> upgrade_firmware(std::vector<FirmwareEntry> targets,
    const UpgradeOptions& options={},std::function<void(const UpgradeProgress&)> progress={},
    const std::atomic<bool>* cancelled=nullptr);
}
