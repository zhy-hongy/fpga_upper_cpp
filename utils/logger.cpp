#include "logger.h"
#include <QDir>
#include <QCoreApplication>
#include <QDateTime>
#include <iostream>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger()
    : m_stdout(stdout, QIODevice::WriteOnly)
{
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(logDir);
    m_file.setFileName(logDir + "/app.log");
    if (m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_stdout.setDevice(&m_file);
    }
}

Logger::~Logger() {
    if (m_file.isOpen()) m_file.close();
}

void Logger::setLevel(LogLevel level) {
    QMutexLocker lock(&m_mutex);
    m_level = level;
}

static const char* levelStr(LogLevel level) {
    switch (level) {
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARN";
        case LOG_ERROR:   return "ERROR";
    }
    return "UNKNOWN";
}

void Logger::log(LogLevel level, const QString& message) {
    QMutexLocker lock(&m_mutex);
    if (level < m_level) return;

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString line = QString("[%1] %2 | %3")
        .arg(timestamp)
        .arg(levelStr(level), -7)
        .arg(message);

    // Console
    std::cout << line.toStdString() << std::endl;

    // File
    if (m_file.isOpen()) {
        QTextStream stream(&m_file);
        stream << line << "\n";
        stream.flush();
    }
}

void Logger::info(const QString& message) { log(LOG_INFO, message); }
void Logger::warn(const QString& message) { log(LOG_WARNING, message); }
void Logger::error(const QString& message) { log(LOG_ERROR, message); }
void Logger::debug(const QString& message) { log(LOG_DEBUG, message); }
