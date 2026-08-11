#ifndef FRAME_PACKET_H
#define FRAME_PACKET_H

#include <cstdint>
#include <QByteArray>

// 数据来源标记：标识 FramePacket 中各元数据字段由谁产生。
// 当前 FPGA 数据协议不包含 batchId/lineIndex/encoder/timestamp，
// 禁止伪造业务数据，缺失字段一律置 0 并用 META_SOURCE_UNKNOWN / META_SOURCE_SDK 标注。
enum MetadataSource {
    META_SOURCE_FPGA    = 0,   // 字段由 FPGA 固件上报
    META_SOURCE_SDK     = 1,   // 字段由 SDK / 上位机本地生成
    META_SOURCE_UNKNOWN = 2    // 无法确定来源
};

// 跨层传递的完整数据帧：UDPDataThread -> ProtocolHandler -> CameraSDK。
// 该结构在完成组帧后生成，SDK 侧不重新组帧、不重新解析协议。
struct FramePacket {
    uint32_t       frameId;          // 全局帧号 (SDK 本地递增)
    uint16_t       sensorId;         // 数据来源 Sensor 编号 (1~4)
    uint32_t       batchId;          // 当前批次编号 (FPGA 协议暂无, 置 0)
    uint32_t       lineIndex;        // 批次内行号 (FPGA 协议暂无, 置 0)
    uint64_t       encoder;          // 触发瞬间编码器绝对脉冲值 (FPGA 协议暂无, 置 0)
    uint64_t       timestamp;        // 时间戳 (us, SDK 接收时间)

    MetadataSource frameIdSource;    // frameId 来源标记
    MetadataSource encoderSource;    // encoder 来源标记
    MetadataSource timestampSource;  // timestamp 来源标记

    uint16_t       width;            // 列数 / 点数
    uint16_t       height;           // 行数

    uint16_t       cmd_code;         // FPGA 协议命令码 (aa55 帧第 4-5 字节)，用于 frame_to_info 自动识别 PixelFormat

    QByteArray     data;             // 载荷数据 (图像原始字节)
};

Q_DECLARE_METATYPE(FramePacket)

#endif // FRAME_PACKET_H
