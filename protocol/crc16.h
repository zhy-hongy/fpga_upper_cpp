#ifndef CRC16_H
#define CRC16_H

#include <cstdint>
#include <vector>
#include <QByteArray>

/// CRC-16/MODBUS: poly=0x8005, init=0xFFFF, ref_in=true, ref_out=true, xor_out=0x0000
uint16_t crc16_modbus(const uint8_t* data, size_t len);
uint16_t crc16_modbus(const QByteArray& data);

/// Accumulating variant — continues CRC from a previous result
uint16_t crc16_modbus(const uint8_t* data, size_t len, uint16_t init_crc);

/// Append 2-byte CRC to data (big-endian)
QByteArray add_crc(const QByteArray& data);

/// Verify last 2 bytes are correct CRC
bool verify_crc(const QByteArray& data_with_crc);

#endif // CRC16_H
