#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QMutex>
#include <QTextStream>

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

class Logger {
public:
    static Logger& instance();
    void setLevel(LogLevel level);
    void log(LogLevel level, const QString& message);
    void info(const QString& message);
    void warn(const QString& message);
    void error(const QString& message);
    void debug(const QString& message);

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    QFile m_file;
    QTextStream m_stdout;
    LogLevel m_level = LOG_DEBUG;
    QMutex m_mutex;
};

#define LOG_INFO(msg)    Logger::instance().info(msg)
#define LOG_WARN(msg)    Logger::instance().warn(msg)
#define LOG_ERROR(msg)   Logger::instance().error(msg)
#define LOG_DEBUG(msg)   Logger::instance().debug(msg)

#endif // LOGGER_H
