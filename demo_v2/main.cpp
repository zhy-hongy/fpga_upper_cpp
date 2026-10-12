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
    parser.addOption({"hardware", "Connect FPGA 192.168.10.10 via local 192.168.10.100 and start four-camera 0x0300 capture."});
    parser.addOption({"skip-heartbeat", "With --hardware, confirm communication by camera image/register READ and send no heartbeat commands."});
    parser.addOption({"preview", "With --hardware, save the window after four-camera images arrive or an 8 second deadline, then exit.", "path"});
    parser.addOption({"image-layout", "With --hardware, select local image decoding: fpga_raw, declared, standard, or auto. This connected firmware requires fpga_raw.", "layout", "fpga_raw"});
    parser.addPositionalArgument("screenshot", "PNG path for --smoke-test.", "[screenshot.png]");
    parser.process(app);
    if ((parser.isSet("hardware") && (parser.isSet("simulate") || parser.isSet("smoke-test"))) ||
        ((parser.isSet("skip-heartbeat") || parser.isSet("preview") || parser.isSet("image-layout")) && !parser.isSet("hardware")))
        parser.showHelp(2);
    MainWindow window;
    window.show();
    if (parser.isSet("smoke-test")) {
        const auto args = parser.positionalArguments();
        window.runSmokeTest(args.isEmpty() ? "fpga_demo_v2.png" : args.first());
    } else if (parser.isSet("simulate")) {
        window.startSimulation();
    } else if (parser.isSet("hardware")) {
        // The connected firmware declares standard images while transmitting raw lane-interleaved pixels.
        auto imageLayout=fpga::ImageLayout::FpgaRaw;
        auto layoutName=parser.value("image-layout");
        if(layoutName=="declared")imageLayout=fpga::ImageLayout::Declared;
        else if(layoutName=="standard")imageLayout=fpga::ImageLayout::Standard;
        else if(layoutName=="auto")imageLayout=fpga::ImageLayout::Auto;
        else if(layoutName!="fpga_raw")parser.showHelp(2);
        window.startHardware(parser.isSet("skip-heartbeat"), parser.value("preview"), imageLayout);
    }
    return app.exec();
}
