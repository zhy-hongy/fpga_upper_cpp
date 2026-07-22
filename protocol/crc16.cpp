#include "crc16.h"

// CRC-16/MODBUS lookup table
static uint16_t crc16_table[256];
static bool table_initialized = false;

static void init_crc16_table() {
    if (table_initialized) return;
    for (uint16_t i = 0; i < 256; i++) {
        uint16_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
        crc16_table[i] = crc;
    }
    table_initialized = true;
}

uint16_t crc16_modbus(const uint8_t* data, size_t len) {
    return crc16_modbus(data, len, 0xFFFF);
}

uint16_t crc16_modbus(const uint8_t* data, size_t len, uint16_t init_crc) {
    init_crc16_table();
    uint16_t crc = init_crc;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc16_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

uint16_t crc16_modbus(const QByteArray& data) {
    return crc16_modbus(reinterpret_cast<const uint8_t*>(data.constData()), data.size());
}

QByteArray add_crc(const QByteArray& data) {
    uint16_t crc = crc16_modbus(data);
    QByteArray result = data;
    result.append(static_cast<char>((crc >> 8) & 0xFF));
    result.append(static_cast<char>(crc & 0xFF));
    return result;
}

bool verify_crc(const QByteArray& data_with_crc) {
    if (data_with_crc.size() < 2) return false;
    QByteArray data = data_with_crc.left(data_with_crc.size() - 2);
    uint16_t expected = static_cast<uint16_t>(
        (static_cast<uint8_t>(data_with_crc[data_with_crc.size() - 2]) << 8) |
        static_cast<uint8_t>(data_with_crc[data_with_crc.size() - 1]));
    return crc16_modbus(data) == expected;
}
