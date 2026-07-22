#include "upgrade_frame.h"
#include "control_frame.h"
#include "crc16.h"
#include <QFile>

QByteArray pack_remote_update_request(int msg_id, int file_size, int file_crc) {
    QByteArray data;
    data.append(static_cast<char>((file_size >> 24) & 0xFF));
    data.append(static_cast<char>((file_size >> 16) & 0xFF));
    data.append(static_cast<char>((file_size >> 8) & 0xFF));
    data.append(static_cast<char>(file_size & 0xFF));
    data.append(static_cast<char>((file_crc >> 24) & 0xFF));
    data.append(static_cast<char>((file_crc >> 16) & 0xFF));
    data.append(static_cast<char>((file_crc >> 8) & 0xFF));
    data.append(static_cast<char>(file_crc & 0xFF));

    return pack_cmd_frame(DEV_FPGA, CMD_REMOTE_UPDATE_REQUEST, msg_id,
                          MSG_TYPE_COMMAND, RW_WRITE, data);
}

QByteArray pack_remote_update_control(int msg_id, uint8_t ctrl_cmd) {
    return pack_cmd_frame(DEV_FPGA, CMD_REMOTE_UPDATE_CONTROL, msg_id,
                          MSG_TYPE_COMMAND, RW_WRITE,
                          QByteArray(1, static_cast<char>(ctrl_cmd)));
}

QByteArray pack_remote_update_data(int msg_id, int chunk_index, const QByteArray& chunk_data) {
    QByteArray data;
    data.append(static_cast<char>((chunk_index >> 8) & 0xFF));
    data.append(static_cast<char>(chunk_index & 0xFF));
    data.append(static_cast<char>((chunk_data.size() >> 8) & 0xFF));
    data.append(static_cast<char>(chunk_data.size() & 0xFF));
    data.append(chunk_data);

    return pack_cmd_frame(DEV_FPGA, CMD_REMOTE_UPDATE_DATA, msg_id,
                          MSG_TYPE_COMMAND, RW_WRITE, data);
}

QByteArray pack_remote_update_result(int msg_id) {
    return pack_cmd_frame(DEV_FPGA, CMD_REMOTE_UPDATE_RESULT, msg_id,
                          MSG_TYPE_COMMAND, RW_READ, QByteArray(2, '\x00'));
}

int compute_file_crc(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) return 0;

    uint16_t crc = 0x0000;
    QByteArray chunk;
    while (!file.atEnd()) {
        chunk = file.read(8192);
        crc = crc16_modbus(reinterpret_cast<const uint8_t*>(chunk.constData()), chunk.size(), crc);
    }
    return crc;
}

QVector<QByteArray> split_firmware_chunks(const QString& file_path) {
    QVector<QByteArray> chunks;
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) return chunks;

    while (!file.atEnd()) {
        QByteArray chunk = file.read(FIRMWARE_CHUNK_SIZE);
        if (!chunk.isEmpty())
            chunks.append(chunk);
    }
    return chunks;
}

int get_firmware_file_size(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    return static_cast<int>(file.size());
}
