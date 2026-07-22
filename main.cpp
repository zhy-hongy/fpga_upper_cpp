#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QFont>
#include <QStyleFactory>

#include "utils/config.h"
#include "utils/logger.h"
#include "ui/main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("FPGA Upper Computer");

    Config config = Config::load();

    // Application icon
    QString logoPath = QCoreApplication::applicationDirPath() + "/assets/logo.jpg";
    if (QFile::exists(logoPath))
        app.setWindowIcon(QIcon(logoPath));

    // Set Fusion style as base (for consistent dark theme)
    app.setStyle(QStyleFactory::create("Fusion"));

    // Dark theme is applied via MainWindow's DARK_QSS stylesheet

    // Also load style.qss if it exists
    QString qssPath = QCoreApplication::applicationDirPath() + "/assets/style.qss";
    // Note: style.qss is not copied to assets/ by default — it's in ui/ dir in Python.
    // We embed the stylesheet in main_window.cpp instead.

    MainWindow window(config);
    window.show();

    LOG_INFO("FPGA 相机上位机启动完成");
    return app.exec();
}
