#ifndef UPGRADE_FRAME_H
#define UPGRADE_FRAME_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include "control_frame.h"

QByteArray pack_remote_update_request(int msg_id, int file_size, int file_crc = 0);
QByteArray pack_remote_update_control(int msg_id, uint8_t ctrl_cmd = UPDATE_CTRL_START);
QByteArray pack_remote_update_data(int msg_id, int chunk_index, const QByteArray& chunk_data);
QByteArray pack_remote_update_result(int msg_id);

int compute_file_crc(const QString& file_path);
QVector<QByteArray> split_firmware_chunks(const QString& file_path);
int get_firmware_file_size(const QString& file_path);

#endif // UPGRADE_FRAME_H
