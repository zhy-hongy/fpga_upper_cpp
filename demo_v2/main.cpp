#include "main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("FPGA Studio · C++ SDK Demo");
    app.setApplicationVersion("2.0.0");
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setFont(QFont("Microsoft YaHei UI", 9));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Run real UDP loopback checks, save a screenshot, and exit."});
    parser.addOption({"simulate", "Connect a real UDP loopback simulator and start four-camera capture."});
    parser.addPositionalArgument("screenshot", "PNG path for --smoke-test.", "[screenshot.png]");
    parser.process(app);
    MainWindow window;
    window.show();
    if (parser.isSet("smoke-test")) {
        const auto args = parser.positionalArguments();
        window.runSmokeTest(args.isEmpty() ? "fpga_demo_v2.png" : args.first());
    } else if (parser.isSet("simulate")) {
        window.startSimulation();
    }
    return app.exec();
}
