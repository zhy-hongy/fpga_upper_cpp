#include "control_frame.h"
#include "crc16.h"
#include <stdexcept>

QByteArray pack_cmd_frame(int device_type, int cmd_code, int msg_id,
                          uint8_t msg_type, uint8_t rw,
                          const QByteArray& data) {
    QByteArray frame;
    auto put16 = [&](uint16_t v) {
        frame.append(static_cast<char>((v >> 8) & 0xFF));
        frame.append(static_cast<char>(v & 0xFF));
    };
    auto put32 = [&](uint32_t v) {
        frame.append(static_cast<char>((v >> 24) & 0xFF));
        frame.append(static_cast<char>((v >> 16) & 0xFF));
        frame.append(static_cast<char>((v >> 8) & 0xFF));
        frame.append(static_cast<char>(v & 0xFF));
    };
    put16(CMD_HEADER);
    put16(static_cast<uint16_t>(device_type));
    put16(static_cast<uint16_t>(cmd_code));
    put16(static_cast<uint16_t>(msg_id));
    frame.append(static_cast<char>(msg_type));
    frame.append(static_cast<char>(rw));
    put32(static_cast<uint32_t>(data.size()));
    frame.append(data);

    // CRC covers everything after CMD_HEADER
    uint16_t crc = crc16_modbus(
        reinterpret_cast<const uint8_t*>(frame.constData()) + 2,
        frame.size() - 2);

    put16(crc);
    put16(CMD_TAIL);
    return frame;
}

ControlFrame unpack_cmd_frame(const QByteArray& frame) {
    if (frame.size() < 18) {
        throw std::runtime_error("Control frame too short: " + std::to_string(frame.size()) + " < 18");
    }

    auto read16 = [&](int offset) -> uint16_t {
        return (static_cast<uint8_t>(frame[offset]) << 8) |
               static_cast<uint8_t>(frame[offset + 1]);
    };
    auto read32 = [&](int offset) -> uint32_t {
        return (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset])) << 24) |
               (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 1])) << 16) |
               (static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 2])) << 8) |
               static_cast<uint32_t>(static_cast<uint8_t>(frame[offset + 3]));
    };

    uint16_t header = read16(0);
    if (header != CMD_HEADER) {
        throw std::runtime_error("Control frame header error");
    }

    uint16_t tail = read16(frame.size() - 2);
    if (tail != CMD_TAIL) {
        throw std::runtime_error("Control frame tail error");
    }

    // Verify CRC: covers everything from offset 2 to (size - 4)
    // Frame structure: [Header(2)] [Data(size-6)] [CRC(2)] [Tail(2)]
    if (frame.size() < 6) {
        throw std::runtime_error("Control frame too short for CRC verification");
    }

    uint16_t received_crc = read16(frame.size() - 4);
    uint16_t calculated_crc = crc16_modbus(
        reinterpret_cast<const uint8_t*>(frame.constData()) + 2,
        frame.size() - 6);

    if (received_crc != calculated_crc) {
        throw std::runtime_error("Control frame CRC error: expected 0x" +
            std::string(QByteArray().setNum(calculated_crc, 16).constData()) +
            ", got 0x" +
            std::string(QByteArray().setNum(received_crc, 16).constData()));
    }

    ControlFrame cf;
    cf.device_type = read16(2);
    cf.cmd_code = read16(4);
    cf.msg_id = read16(6);
    cf.msg_type = static_cast<uint8_t>(frame[8]);
    cf.rw = static_cast<uint8_t>(frame[9]);
    int data_len = static_cast<int>(read32(10));
    cf.data = frame.mid(14, data_len);
    return cf;
}
