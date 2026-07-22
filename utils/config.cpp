#include "config.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

QString Config::configDir() {
    QString base = QCoreApplication::applicationDirPath();
    QDir dir(base + "/config");
    if (!dir.exists()) dir.mkpath(".");
    return dir.absolutePath();
}

Config Config::load(const QString& path) {
    QString filePath = path.isEmpty() ? configDir() + "/settings.json" : path;
    QFile file(filePath);
    Config cfg;

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();

        if (obj.contains("target_host")) cfg.target_host = obj["target_host"].toString();
        if (obj.contains("target_port")) cfg.target_port = obj["target_port"].toInt();
        if (obj.contains("host_ip")) cfg.host_ip = obj["host_ip"].toString();
        if (obj.contains("control_port")) cfg.control_port = obj["control_port"].toInt();
        if (obj.contains("data_port")) cfg.data_port = obj["data_port"].toInt();
        if (obj.contains("recv_buffer_size")) cfg.recv_buffer_size = obj["recv_buffer_size"].toInt();
        if (obj.contains("default_exposure")) cfg.default_exposure = obj["default_exposure"].toInt();
        if (obj.contains("default_gain")) cfg.default_gain = obj["default_gain"].toInt();
        if (obj.contains("default_frame_rate")) cfg.default_frame_rate = obj["default_frame_rate"].toInt();
        if (obj.contains("theme")) cfg.theme = obj["theme"].toString();
        if (obj.contains("window_title")) cfg.window_title = obj["window_title"].toString();

        file.close();
    }
    return cfg;
}

void Config::save(const QString& path) const {
    QString filePath = path.isEmpty() ? configDir() + "/settings.json" : path;
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QJsonObject obj;
    obj["target_host"] = target_host;
    obj["target_port"] = target_port;
    obj["host_ip"] = host_ip;
    obj["control_port"] = control_port;
    obj["data_port"] = data_port;
    obj["recv_buffer_size"] = recv_buffer_size;
    obj["default_exposure"] = default_exposure;
    obj["default_gain"] = default_gain;
    obj["default_frame_rate"] = default_frame_rate;
    obj["theme"] = theme;
    obj["window_title"] = window_title;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        file.close();
    }
}
