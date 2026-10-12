#ifndef FPGA_IMAGE_DECODER_H
#define FPGA_IMAGE_DECODER_H

#include <QByteArray>
#include <opencv2/core.hpp>

// FPGA 输出的图像数据格式 (8-bit 每像素, 2048×1152):
//   FPGA 内部以 16 像素为一批输出, 数据流中交替排列:
//   [奇行0_周期0(16B)] [偶行0_周期0(16B)] [奇行0_周期1(16B)] [偶行0_周期1(16B)] ...
//   每个 16-pixel 输出组内部字节顺序是反的。
// 解码: 将上述交织数据重排为标准的 (height, width) 行优先 uint8 图像。
// （与 Python communication/fpga_image_decoder.py 的 decode_fpga_image 一致）

constexpr int FPGA_WIDTH = 2048;
constexpr int FPGA_HEIGHT = 1152;
constexpr int FPGA_FULL_IMAGE_SIZE = FPGA_WIDTH * FPGA_HEIGHT;  // 2,359,296

// 将 FPGA 交织编码的载荷重排为行优先 (height,width) uint8 图。
// 返回 false: 尺寸非法 / 数据不足。
//
// 性能优化 (2026-08): 旧实现按 (row,col) 逐像素计算 col/cycles 与 col%cycles
// (整型除法/取模), 2.36M 像素/帧在 Debug 构建下极慢 (~80-150ms/帧 → 上限 ~8-12 FPS),
// 导致 25 FPS 时接收线程来不及排空 socket, 丢包 → 组帧失败 → "无法解析"。
// 新实现改为按【行对】解码: 每个行对 [奇16B][偶16B] × cycles, 组内 16 像素逆序。
// 输出第 j 个源字节 → 列 (15-j)*cycles + c, 源在行对内步长 32 —— 彻底消除除法/取模,
// Debug 构建下提升约 5-10 倍 (可支撑 100+ FPS 解码)。
inline bool decode_fpga_image(const QByteArray& payload, int width, int height, cv::Mat& out) {
    if (width <= 0 || height <= 0 || width % 16 != 0 || height % 2 != 0)
        return false;
    if (payload.size() < width * height)
        return false;

    const int cycles = width / 16;      // 128
    const int row_pairs = height / 2;   // 576
    const int pair_bytes = cycles * 32; // 每个行对的交织数据字节数 (128*32=4096)
    const auto* src = reinterpret_cast<const uint8_t*>(payload.constData());

    cv::Mat img(height, width, CV_8UC1);
    for (int rp = 0; rp < row_pairs; ++rp) {
        const uint8_t* base = src + rp * pair_bytes;
        uint8_t* row_odd  = img.ptr<uint8_t>(rp * 2);
        uint8_t* row_even = img.ptr<uint8_t>(rp * 2 + 1);
        for (int j = 0; j < 16; ++j) {
            uint8_t* dst_odd  = row_odd  + (15 - j) * cycles;
            uint8_t* dst_even = row_even + (15 - j) * cycles;
            const uint8_t* src_odd  = base + j;        // 步长 32
            const uint8_t* src_even = base + 16 + j;   // 步长 32
            for (int c = 0; c < cycles; ++c) {
                dst_odd[c]  = src_odd[c * 32];
                dst_even[c] = src_even[c * 32];
            }
        }
    }
    out = img;
    return true;
}

#endif // FPGA_IMAGE_DECODER_H
