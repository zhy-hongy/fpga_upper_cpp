#include "mock_protocol_handler.h"

#include <QDateTime>

#include <opencv2/imgproc.hpp>

MockProtocolHandler::MockProtocolHandler(const Config& config, QObject* parent)
    : ProtocolHandler(config, DEV_SENSOR1, parent),
      m_frame_timer(new QTimer(this)) {
    // 基类已创建 UDPWorker/UDPDataThread 对象，但【不 start_all()】：
    // 两个线程不启动、不 bind、不 new QUdpSocket → 无任何 UDP 活动。
    connect(m_frame_timer, &QTimer::timeout, this, [this] { generate_frame(); });
}

void MockProtocolHandler::start_mock_frames() {
    m_capturing.store(true);
    m_frame_timer->start(40);   // 40ms/帧
}

void MockProtocolHandler::stop_mock_frames() {
    m_frame_timer->stop();
    m_capturing.store(false);
}

void MockProtocolHandler::generate_frame() {
    if (!m_capturing.load()) return;

    const uint32_t frameId = m_frame_id.fetch_add(1) + 1;
    FramePacket pkt;
    pkt.frameId = frameId;
    pkt.sensorId = static_cast<uint16_t>(((frameId - 1) % 4) + 1);   // 1~4 循环
    pkt.batchId = 0;
    pkt.lineIndex = 0;
    pkt.encoder = 0;
    pkt.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;  // us
    pkt.frameIdSource = META_SOURCE_SDK;
    pkt.timestampSource = META_SOURCE_SDK;
    pkt.encoderSource = META_SOURCE_UNKNOWN;
    pkt.width = 1280;
    pkt.height = 1024;
    pkt.data = build_mono8_frame(frameId, pkt.sensorId);

    // 直连连接 → SDK DataDispatcher::enqueue（线程安全），CallbackWorker 消费。
    emit frame_ready(pkt);
}

QByteArray MockProtocolHandler::build_mono8_frame(uint32_t frameId, uint16_t sensorId) {
    cv::Mat img(1024, 1280, CV_8UC1);
    // 水平渐变：列 x → 灰度 0~255
    for (int y = 0; y < img.rows; ++y) {
        uchar* row = img.ptr<uchar>(y);
        for (int x = 0; x < img.cols; ++x)
            row[x] = static_cast<uchar>((x * 255) / (img.cols - 1));
    }
    // frameId 文字区域（验证帧内容确实在变化）
    cv::putText(img, cv::format("F%u S%u", static_cast<unsigned>(frameId),
                                static_cast<unsigned>(sensorId)),
                cv::Point(30, 80), cv::FONT_HERSHEY_SIMPLEX, 2.0,
                cv::Scalar(255), 3, cv::LINE_AA);
    return QByteArray(reinterpret_cast<const char*>(img.data),
                      static_cast<int>(img.total()));
}
