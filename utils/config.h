#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QJsonObject>

struct Config {
    // Network
    QString target_host = "192.168.10.10";
    int target_port = 60000;
    QString host_ip = "0.0.0.0";
    int control_port = 60000;
    int data_port = 60000;
    int recv_buffer_size = 128 * 1024 * 1024; // 128MB: 对标 Python DATA_RCVBUF, 吸收高帧率突发

    // Camera
    int default_exposure = 10000;
    int default_gain = 10;
    int default_frame_rate = 30;

    // UI
    QString theme = "dark";
    QString window_title = "激光三维轮廓测量软件";

    static Config load(const QString& path = "");
    void save(const QString& path = "") const;

private:
    static QString configDir();
};

#endif // CONFIG_H