#ifndef CONTROL_FRAME_H
#define CONTROL_FRAME_H

#include <QByteArray>
#include <QMap>

// Frame constants
constexpr uint16_t CMD_HEADER = 0xAA55;
constexpr uint16_t CMD_TAIL = 0xFAFA;
constexpr uint8_t CMD_HEADER_BYTES[] = {0xAA, 0x55};
constexpr uint8_t CMD_TAIL_BYTES[] = {0xFA, 0xFA};

// Message types
constexpr uint8_t MSG_TYPE_COMMAND = 0x00;
constexpr uint8_t MSG_TYPE_RESPONSE = 0x01;
constexpr uint8_t MSG_TYPE_EVENT = 0x02;

// Read/Write flags
constexpr uint8_t RW_WRITE = 0x40;
constexpr uint8_t RW_READ = 0x80;

// Device types
constexpr int DEV_FPGA = 0x0000;
constexpr int DEV_SENSOR1 = 0x0001;
constexpr int DEV_SENSOR2 = 0x0002;
constexpr int DEV_SENSOR3 = 0x0003;
constexpr int DEV_SENSOR4 = 0x0004;

// Command codes（对齐 protocol/通讯协议.md）
// 注意：0x0011 START_STOP 已从协议删除，采集/停止统一归入 CMD_BATCH_CONTROL(0x0204)。
constexpr int CMD_EXPOSURE_TIME  = 0x0001;
constexpr int CMD_GAIN           = 0x0002;
constexpr int CMD_ROI_X          = 0x0003;   // 设置/获取X轴Roi（当前 SDK 未实现，仅供协议分析器识别）
constexpr int CMD_CAMERA_VERSION = 0x0007;   // 获取相机固件版本
constexpr int CMD_SAMPLE_RATE    = 0x000A;
constexpr int CMD_TRIGGER_MODE   = 0x0101;
constexpr int CMD_ENCODER_TYPE   = 0x0102;
constexpr int CMD_ENCODER_MODE   = 0x0103;
constexpr int CMD_ENCODER_INTERVAL = 0x0104;
constexpr int CMD_BATCH_LENGTH   = 0x0201;
constexpr int CMD_GET_BATCH_DATA = 0x0202;
constexpr int CMD_EXTERNAL_TRIGGER = 0x0203;
constexpr int CMD_BATCH_CONTROL  = 0x0204;
constexpr int CMD_RESET_BUFFER   = 0x0205;
constexpr int CMD_NETWORK_CONFIG = 0x0501;   // 配置/获取网络参数
constexpr int CMD_FPGA_VERSION   = 0x0502;   // 获取 FPGA 固件版本
constexpr int CMD_DEVICE_SERIAL  = 0x0503;   // 设置/获取设备序列号

// Remote firmware upgrade
constexpr int CMD_REMOTE_UPDATE_REQUEST = 0x0020;
constexpr int CMD_REMOTE_UPDATE_CONTROL = 0x0021;
constexpr int CMD_REMOTE_UPDATE_DATA = 0x0022;
constexpr int CMD_REMOTE_UPDATE_RESULT = 0x0023;

// 数据帧类型（FPGA 通过 cmd_code 区分上行数据帧类型，供 frame_to_info 自动识别 PixelFormat）
// 注意：这些值是 FPGA 固件协议约定的，如需修改请与 FPGA 固件保持一致。
constexpr int CMD_DATA_IMAGE_2D   = 0x0301;  // 2D 灰度图 (PIXEL_FMT_MONO8)
constexpr int CMD_DATA_CENTERLINE = 0x0302;  // 激光中心线 (PIXEL_FMT_CENTERLINE_F32)
constexpr int CMD_DATA_POINTCLOUD = 0x0303;  // 3D 点云 (PIXEL_FMT_POINTCLOUD_XYZ)

constexpr uint8_t UPDATE_CTRL_START = 0x01;
constexpr uint8_t UPDATE_CTRL_ABORT = 0x00;

constexpr uint8_t UPDATE_STATUS_SUCCESS = 0x00;
constexpr uint8_t UPDATE_STATUS_FAIL = 0x01;
constexpr uint8_t UPDATE_STATUS_IN_PROGRESS = 0x02;
constexpr uint8_t UPDATE_STATUS_READY = 0x03;

constexpr int FIRMWARE_CHUNK_SIZE = 1024;

struct ControlFrame {
    int device_type = 0;
    int cmd_code = 0;
    int msg_id = 0;
    uint8_t msg_type = 0;
    uint8_t rw = 0;
    QByteArray data;
};

// Pack/unpack control command frames
QByteArray pack_cmd_frame(int device_type, int cmd_code, int msg_id,
                          uint8_t msg_type = MSG_TYPE_COMMAND,
                          uint8_t rw = RW_WRITE,
                          const QByteArray& data = QByteArray());

ControlFrame unpack_cmd_frame(const QByteArray& frame);

#endif // CONTROL_FRAME_H
