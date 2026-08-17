#include "protocol_analyzer.h"
#include "protocol/transport_frame.h"
#include "protocol/control_frame.h"
#include "protocol/crc16.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFont>

static const char* DEVICE_NAMES[] = {
    "FPGA", "Sensor1", "Sensor2", "Sensor3", "Sensor4"
};

static const char* cmd_name(int code) {
    switch (code) {
        case 0x0001: return "曝光时间 (EXPOSURE_TIME)";
        case 0x0002: return "增益 (GAIN)";
        case 0x0003: return "设置/获取X轴Roi (ROI_X)";
        case 0x0007: return "获取相机固件版本 (CAMERA_VERSION)";
        case 0x000A: return "采样率 (SAMPLE_RATE)";
        case 0x000C: return "设置图像模式 (IMAGE_MODE)";
        case 0x0011: return "启停采集 (START_STOP, legacy/已废弃)";
        case 0x0101: return "触发模式 (TRIGGER_MODE)";
        case 0x0102: return "编码器类型 (ENCODER_TYPE)";
        case 0x0103: return "编码器输入模式 (ENCODER_MODE)";
        case 0x0104: return "编码器触发间隔 (ENCODER_INTERVAL)";
        case 0x0201: return "批处理长度 (BATCH_LENGTH)";
        case 0x0202: return "获取批处理数据 (GET_BATCH_DATA)";
        case 0x0203: return "外部触发开始 (EXTERNAL_TRIGGER)";
        case 0x0204: return "启动/停止批处理 (BATCH_CONTROL)";
        case 0x0205: return "重置缓存数据 (RESET_BUFFER)";
        case 0x0301: return "获取相机数据 图像 (DATA_IMAGE_2D)";
        case 0x0302: return "获取相机数据 中心线 (DATA_CENTERLINE)";
        case 0x0303: return "获取相机数据 3D点云 (DATA_POINTCLOUD)";
        case 0x0501: return "配置网络参数 (NETWORK_CONFIG)";
        case 0x0502: return "获取FPGA固件版本 (FPGA_VERSION)";
        case 0x0503: return "设置/获取设备序列号 (DEVICE_SERIAL)";
        case 0x0020: return "远程更新请求 (UPDATE_REQUEST)";
        case 0x0021: return "远程更新控制 (UPDATE_CONTROL)";
        case 0x0022: return "远程更新数据 (UPDATE_DATA)";
        case 0x0023: return "远程更新结果 (UPDATE_RESULT)";
        default: return "未知命令";
    }
}

static const char* msg_type_name(uint8_t type) {
    switch (type) {
        case 0x00: return "请求 (COMMAND)";
        case 0x01: return "应答 (RESPONSE)";
        case 0x02: return "推送 (PUSH)";
        default: return "未知";
    }
}

static const char* rw_name(uint8_t rw) {
    switch (rw) {
        case 0x40: return "写 (WRITE)";
        case 0x80: return "读 (READ)";
        default: return "未知";
    }
}

ProtocolAnalyzerDialog::ProtocolAnalyzerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("协议分析器");
    setMinimumSize(900, 650);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);

    auto* main_layout = new QVBoxLayout(this);

    // Input group
    auto* inp_group = new QGroupBox("输入十六进制数据 (粘贴 FPGA 发送/接收的原始帧)");
    auto* inp_layout = new QVBoxLayout(inp_group);

    m_text_input = new QPlainTextEdit();
    m_text_input->setPlaceholderText(
        "在此粘贴十六进制数据，如:\n"
        "abab9cd0000100010000000000000014aa5500000503c1cb00800000000200008dfffafa7ae1dcdc");
    m_text_input->setMaximumHeight(80);
    QFont mono("Consolas", 10);
    m_text_input->setFont(mono);
    inp_layout->addWidget(m_text_input);

    auto* btn_layout = new QHBoxLayout();
    auto* btn_parse = new QPushButton("解析");
    btn_parse->setObjectName("btn_primary");
    auto* btn_clear = new QPushButton("清空");
    btn_layout->addWidget(btn_parse);
    btn_layout->addWidget(btn_clear);
    btn_layout->addStretch();
    inp_layout->addLayout(btn_layout);
    main_layout->addWidget(inp_group);

    // Output area
    m_output = new QTextEdit();
    m_output->setReadOnly(true);
    m_output->setFont(QFont("Consolas", 10));
    m_output->setStyleSheet(
        "QTextEdit { background-color: #1a1a2e; color: #e0e0e0; border: 1px solid #333; border-radius: 4px; padding: 8px; }");
    main_layout->addWidget(m_output);

    connect(btn_parse, &QPushButton::clicked, this, &ProtocolAnalyzerDialog::parse);
    connect(btn_clear, &QPushButton::clicked, this, &ProtocolAnalyzerDialog::clear);
}

void ProtocolAnalyzerDialog::clear() {
    m_text_input->clear();
    m_output->clear();
}

void ProtocolAnalyzerDialog::parse() {
    QString raw_text = m_text_input->toPlainText().trimmed();
    if (raw_text.isEmpty()) {
        m_output->setHtml("<span style='color:#ff6b6b;'>[错误] 输入为空</span>");
        return;
    }

    // Clean input
    QString hex_str = raw_text;
    hex_str.replace(" ", "").replace("\n", "").replace("\r", "");
    hex_str.replace("0x", "").replace("0X", "");

    // Validate hex characters
    for (const QChar& c : hex_str) {
        if (!c.isDigit() && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) {
            m_output->setHtml("<span style='color:#ff6b6b;'>[错误] 输入包含非十六进制字符</span>");
            return;
        }
    }

    if (hex_str.length() % 2 != 0) {
        m_output->setHtml("<span style='color:#ff6b6b;'>[错误] 十六进制长度为奇数</span>");
        return;
    }

    QByteArray data = QByteArray::fromHex(hex_str.toLatin1());

    QString html;
    html += QString("<b>输入长度:</b> %1 字节 (%2 hex 字符)<br>").arg(data.size()).arg(hex_str.length());
    html += QString("<b>原始Hex:</b> <span style='color:#888;font-size:11px;'>%1</span><br><br>").arg(hex_str);

    // Layer 1: Transport layer
    html += "<h3 style='color:#4ecdc4;'>━━━ 传输层 (0xABAB 外包装) ━━━</h3>";
    html += parse_transport_layer(data);

    m_output->setHtml(html);
}

QString ProtocolAnalyzerDialog::parse_transport_layer(const QByteArray& data) {
    QString result;

    if (data.size() < 2) {
        return "<span style='color:#ff6b6b;'>[错误] 数据不足 2 字节</span>";
    }

    auto read16 = [&](int offset) -> uint16_t {
        return (static_cast<uint8_t>(data[offset]) << 8) | static_cast<uint8_t>(data[offset + 1]);
    };

    uint16_t header = read16(0);
    result += QString("<b>帧头:</b> 0x%1 (%2 %3)<br>")
        .arg(header, 4, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[0]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[1]), 2, 16, QChar('0'));

    if (header != TRANSPORT_HEADER) {
        result += QString("<span style='color:#ff6b6b;'>  ✗ 帧头错误! 期望 0x%1, 实际 0x%2</span><br>")
            .arg(TRANSPORT_HEADER, 4, 16, QChar('0')).arg(header, 4, 16, QChar('0'));
        return result;
    }
    result += "  <span style='color:#4ecdc4;'>✓ 帧头正确</span><br>";

    if (data.size() < TRANSPORT_OVERHEAD) {
        result += QString("<span style='color:#ff6b6b;'>[错误] 传输帧长度不足: %1 &lt; 最小 %2</span><br>")
            .arg(data.size()).arg(TRANSPORT_OVERHEAD);
        return result;
    }

    uint16_t tail = read16(data.size() - 2);
    result += QString("<b>帧尾:</b> 0x%1 (%2 %3)<br>")
        .arg(tail, 4, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[data.size() - 2]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[data.size() - 1]), 2, 16, QChar('0'));

    if (tail != TRANSPORT_TAIL) {
        result += QString("<span style='color:#ff6b6b;'>  ✗ 帧尾错误! 期望 0x%1, 实际 0x%2</span><br>")
            .arg(TRANSPORT_TAIL, 4, 16, QChar('0')).arg(tail, 4, 16, QChar('0'));
        return result;
    }
    result += "  <span style='color:#4ecdc4;'>✓ 帧尾正确</span><br>";

    // CRC check
    uint16_t crc_received = read16(data.size() - 4);
    uint16_t crc_computed = crc16_modbus(reinterpret_cast<const uint8_t*>(data.data()) + 2, data.size() - 6);
    result += QString("<b>CRC16:</b> 收到 0x%1, 计算 0x%2 (校验范围: 包ID~Data, 共 %3 字节)<br>")
        .arg(crc_received, 4, 16, QChar('0')).arg(crc_computed, 4, 16, QChar('0'))
        .arg(data.size() - 6);
    result += (crc_received == crc_computed)
        ? "  <span style='color:#4ecdc4;'>✓ CRC 校验通过</span><br>"
        : "  <span style='color:#ff6b6b;'>✗ CRC 校验失败!</span><br>";

    // Transport fields
    uint16_t packet_id = read16(2);
    uint16_t total_packets = read16(4);
    uint16_t current_packet = read16(6);
    uint32_t retransmit = (static_cast<uint8_t>(data[8]) << 24) |
                          (static_cast<uint8_t>(data[9]) << 16) |
                          (static_cast<uint8_t>(data[10]) << 8) |
                          static_cast<uint8_t>(data[11]);
    uint16_t data_len = read16(16);
    QByteArray inner_data = data.mid(18, data_len);

    result += "<br><b>── 传输层字段 ──</b><br>";
    result += QString("<b>唯一帧ID:</b> 0x%1 (%2)<br>").arg(packet_id, 4, 16, QChar('0')).arg(packet_id);
    result += QString("<b>总包数:</b> %1<br>").arg(total_packets);
    result += QString("<b>当前包数:</b> %1<br>").arg(current_packet);

    if (retransmit == RETRANSMIT_REQUEST)
        result += "<b>重传说明:</b> <span style='color:#f0c040;'>请求重传 (1)</span><br>";
    else
        result += "<b>重传说明:</b> <span style='color:#4ecdc4;'>正常发送 (0)</span><br>";

    uint32_t empty = (static_cast<uint8_t>(data[12]) << 24) |
                     (static_cast<uint8_t>(data[13]) << 16) |
                     (static_cast<uint8_t>(data[14]) << 8) |
                     static_cast<uint8_t>(data[15]);
    result += QString("<b>保留字段:</b> 0x%1<br>").arg(empty, 8, 16, QChar('0'));
    result += QString("<b>数据长度:</b> %1 字节 (0x%2)<br>").arg(data_len).arg(data_len, 4, 16, QChar('0'));
    result += QString("<b>Data (Hex):</b> <span style='color:#aaa;font-size:10px;'>%1</span><br>")
        .arg(QString(inner_data.toHex()));

    if (total_packets > 1)
        result += QString("<span style='color:#f0c040;'>⚠ 多包传输: 当前为第 %1/%2 包</span><br>")
            .arg(current_packet).arg(total_packets);
    else
        result += "  <span style='color:#4ecdc4;'>单包传输</span><br>";

    result += "<br>";
    result += "<h3 style='color:#ff6b6b;'>━━━ 内部协议帧 (0xAA55 帧) ━━━</h3>";
    result += parse_inner_frame(inner_data);

    return result;
}

QString ProtocolAnalyzerDialog::parse_inner_frame(const QByteArray& data) {
    QString result;

    if (data.size() < 2) {
        return "<span style='color:#ff6b6b;'>[错误] 内部帧数据不足 2 字节</span>";
    }

    auto read16 = [&](int offset) -> uint16_t {
        return (static_cast<uint8_t>(data[offset]) << 8) | static_cast<uint8_t>(data[offset + 1]);
    };
    auto read32 = [&](int offset) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint8_t>(data[offset])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(data[offset + 3]));
    };

    uint16_t header = read16(0);
    result += QString("<b>帧头:</b> 0x%1 (%2 %3)<br>")
        .arg(header, 4, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[0]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[1]), 2, 16, QChar('0'));

    if (header != CMD_HEADER) {
        result += QString("<span style='color:#ff6b6b;'>  ✗ 帧头错误! 期望 0x%1, 实际 0x%2</span><br>")
            .arg(CMD_HEADER, 4, 16, QChar('0')).arg(header, 4, 16, QChar('0'));
        return result;
    }
    result += "  <span style='color:#ff6b6b;'>✓ 帧头正确</span><br>";

    if (data.size() < 18) {
        result += QString("<span style='color:#ff6b6b;'>[错误] 内部帧长度不足: %1 &lt; 最小 18</span><br>").arg(data.size());
        return result;
    }

    uint16_t tail = read16(data.size() - 2);
    result += QString("<b>帧尾:</b> 0x%1 (%2 %3)<br>")
        .arg(tail, 4, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[data.size() - 2]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[data.size() - 1]), 2, 16, QChar('0'));

    if (tail != CMD_TAIL) {
        result += QString("<span style='color:#ff6b6b;'>  ✗ 帧尾错误! 期望 0x%1, 实际 0x%2</span><br>")
            .arg(CMD_TAIL, 4, 16, QChar('0')).arg(tail, 4, 16, QChar('0'));
        return result;
    }
    result += "  <span style='color:#ff6b6b;'>✓ 帧尾正确</span><br>";

    // CRC check
    int data_len = static_cast<int>(read32(10));
    int body_len = 14 + data_len;
    if (body_len + 2 > data.size()) {
        result += "<span style='color:#ff6b6b;'>[错误] 数据长度字段超出实际帧长</span><br>";
        return result;
    }
    uint16_t crc_received = read16(body_len);
    uint16_t crc_computed = crc16_modbus(reinterpret_cast<const uint8_t*>(data.data()) + 2, body_len - 2);
    result += QString("<b>CRC16:</b> 收到 0x%1, 计算 0x%2 (校验范围: Dev~Data, 共 %3 字节)<br>")
        .arg(crc_received, 4, 16, QChar('0')).arg(crc_computed, 4, 16, QChar('0'))
        .arg(body_len - 2);
    result += (crc_received == crc_computed)
        ? "  <span style='color:#ff6b6b;'>✓ CRC 校验通过</span><br>"
        : "  <span style='color:#ff6b6b;'>✗ CRC 校验失败!</span><br>";

    // Parse fields
    uint16_t device_type = read16(2);
    uint16_t cmd_code = read16(4);
    uint16_t msg_id = read16(6);
    uint8_t msg_type = static_cast<uint8_t>(data[8]);
    uint8_t rw = static_cast<uint8_t>(data[9]);
    QByteArray payload_data = data.mid(14, data_len);

    const char* dev_name = (device_type <= 4) ? DEVICE_NAMES[device_type] : "未知设备";

    result += "<br><b>── 内部帧字段 ──</b><br>";
    result += QString("<b>设备类型:</b> 0x%1 → <span style='color:#f0c040;'>%2</span><br>")
        .arg(device_type, 4, 16, QChar('0')).arg(dev_name);
    result += QString("<b>功能码:</b> 0x%1 → <span style='color:#f0c040;'>%2</span><br>")
        .arg(cmd_code, 4, 16, QChar('0')).arg(cmd_name(cmd_code));
    result += QString("<b>消息序号:</b> 0x%1 (%2)<br>").arg(msg_id, 4, 16, QChar('0')).arg(msg_id);
    result += QString("<b>消息类型:</b> 0x%1 → <span style='color:#f0c040;'>%2</span><br>")
        .arg(msg_type, 2, 16, QChar('0')).arg(msg_type_name(msg_type));
    result += QString("<b>读写控制:</b> 0x%1 → <span style='color:#f0c040;'>%2</span><br>")
        .arg(rw, 2, 16, QChar('0')).arg(rw_name(rw));
    result += QString("<b>数据长度:</b> %1 字节 (0x%2)<br>").arg(data_len).arg(data_len, 4, 16, QChar('0'));
    if (!payload_data.isEmpty())
        result += QString("<b>Data:</b> <span style='color:#aaa;'>%1</span> (%2 字节)<br>")
            .arg(QString(payload_data.toHex())).arg(payload_data.size());
    else
        result += "<b>Data:</b> (空)<br>";

    result += "<br>";
    result += "<h3 style='color:#4ecdc4;'>━━━ 解析摘要 ━━━</h3>";

    const char* action = (msg_type == 0x00) ? "请求" : (msg_type == 0x01) ? "应答" : "推送";
    const char* op = (rw == 0x80) ? "读取" : "写入";

    result += QString("设备 <b>%1</b> (0x%2) 发出 <b>%3</b>: <b>%4 %5</b> (0x%6)<br>")
        .arg(dev_name).arg(device_type, 4, 16, QChar('0'))
        .arg(action).arg(op).arg(cmd_name(cmd_code)).arg(cmd_code, 4, 16, QChar('0'));

    if (!payload_data.isEmpty() && data_len >= 2) {
        uint16_t val = read16(0); // from payload_data start
        result += QString("数据值: 0x%1 (%2)<br>").arg(val, 4, 16, QChar('0')).arg(val);
    }

    return result;
}
