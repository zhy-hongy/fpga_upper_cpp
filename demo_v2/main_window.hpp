#pragma once
#include "fpga/device.hpp"
#include <QMainWindow>
#include <QImage>
#include <QPointF>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <map>
#include <thread>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QTabWidget;
class QTimer;
class FrameView;
class ProfileView;
class QCheckBox;
namespace fpga { class FocusDetector; }

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void runSmokeTest(const QString& screenshotPath);
    void startSimulation();
    void startHardware(bool skipHeartbeat, const QString& previewPath = {},
                       fpga::ImageLayout imageLayout = fpga::ImageLayout::FpgaRaw);

private:
    void buildUi();
    QWidget* buildParameters();
    QWidget* buildTrigger();
    QWidget* buildInterpolation();
    QWidget* buildSystem();
    QWidget* buildRegisters();
    QWidget* buildAnalysis();
    void showFocus(const fpga::Image& image);
    void connectDevice();
    void disconnectDevice();
    void setConnected(bool value);
    void rebuildParameters();
    void updateConfirmedParameter(uint16_t command);
    void readParameters(bool all);
    void writeParameter();
    void capture(bool enabled);
    void singleCapture();
    void receiveCamera(std::shared_ptr<const fpga::CameraEvent> event);
    void receiveBatch(const fpga::BatchEvent& event);
    void updateStatistics();
    void updateInterpolationPreview();
    void log(const QString& message, bool error = false);
    void enqueue(std::function<void()> job);
    void submit(const QString& label, std::function<fpga::Response()> operation,
                std::function<void(const fpga::Response&)> done = {});
    std::vector<uint16_t> cameraTargets() const;
    uint16_t selectedCamera() const;
    fpga::Bytes encodeParameter(uint16_t command, const QString& value) const;
    QString decodeParameter(uint16_t command, const fpga::Bytes& value) const;
    void exportCurrentData();

    fpga::Device device_;
    fpga::Simulator simulator_;
    std::thread worker_;
    std::mutex queueMutex_;
    std::mutex lifecycleMutex_;
    std::condition_variable queueCv_;
    std::deque<std::function<void()>> jobs_;
    bool workerStop_ = false;
    bool connected_ = false;
    bool connecting_ = false;
    bool heartbeatEnabled_ = true;
    bool capturing_ = false;
    bool smoke_ = false;
    bool smokeFailed_ = false;
    qint64 lastHeartbeatMs_ = 0;
    int heartbeatTick_ = 0;
    QString smokePath_;
    std::array<FrameView*, 4> frames_{};
    std::array<uint64_t, 4> frameCounts_{};
    std::array<std::atomic<bool>,4> renderPending_{};
    std::array<std::shared_ptr<const fpga::CameraEvent>, 4> lastFrames_{};
    uint64_t batchCount_ = 0;
    fpga::BatchData lastBatch_;
    std::map<std::pair<uint16_t,uint16_t>,fpga::Bytes> confirmedParameters_;
    std::atomic<bool> upgradeCancelled_{false};
    QComboBox* mode_ = nullptr;
    QCheckBox* skipHeartbeat_ = nullptr;
    QComboBox* target_ = nullptr;
    QComboBox* imageRequest_ = nullptr;
    QComboBox* imageLayout_ = nullptr;
    QSpinBox* captureInterval_ = nullptr;
    QComboBox* plotMode_ = nullptr;
    QComboBox* triggerMode_ = nullptr;
    QComboBox* encoderType_ = nullptr;
    QComboBox* encoderMode_ = nullptr;
    QComboBox* firmwareTarget_ = nullptr;
    QLineEdit* deviceIp_ = nullptr;
    QLineEdit* localIp_ = nullptr;
    QSpinBox* devicePort_ = nullptr;
    QSpinBox* localPort_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QLabel* connectionStatus_ = nullptr;
    QLabel* frameMetric_ = nullptr;
    QLabel* trafficMetric_ = nullptr;
    QLabel* integrityMetric_ = nullptr;
    QLabel* batchStatus_ = nullptr;
    QLabel* interpPreview_ = nullptr;
    QLabel* bottomStatus_ = nullptr;
    QPlainTextEdit* logs_ = nullptr;
    QTableWidget* parameters_ = nullptr;
    QTabWidget* controls_ = nullptr;
    QSpinBox* encoderInterval_ = nullptr;
    QDoubleSpinBox* sampleRate_ = nullptr;
    std::array<QSpinBox*, 4> delays_{};
    QSpinBox* batchLength_ = nullptr;
    QSpinBox* batchIndex_ = nullptr;
    QDoubleSpinBox* interpStart_ = nullptr;
    QDoubleSpinBox* interpEnd_ = nullptr;
    QDoubleSpinBox* interpInterval_ = nullptr;
    QSpinBox* interpCount_ = nullptr;
    std::array<QLineEdit*, 5> network_{};
    QLineEdit* firmwarePath_ = nullptr;
    QLineEdit* firmwareAddress_ = nullptr;
    QSpinBox* firmwarePort_ = nullptr;
    QProgressBar* firmwareProgress_ = nullptr;
    QSpinBox* rawDevice_ = nullptr;
    QLineEdit* rawCommand_ = nullptr;
    QComboBox* rawRw_ = nullptr;
    QPlainTextEdit* rawPayload_ = nullptr;
    QPlainTextEdit* rawResponse_ = nullptr;
    ProfileView* profile_ = nullptr;
    QTimer* statsTimer_ = nullptr;
    std::unique_ptr<fpga::FocusDetector> focusDetector_;
    QCheckBox* focusLive_ = nullptr;
    QCheckBox* focusWhole_ = nullptr;
    std::array<QSpinBox*,4> focusRoi_{};
    QLabel* focusStatus_ = nullptr;
    QLineEdit* nativeDll_ = nullptr;
    QLineEdit* nativeKernel_ = nullptr;
    QLineEdit* nativeNeural_ = nullptr;
    QSpinBox* nativeThreshold_ = nullptr;
    QPlainTextEdit* comparison_ = nullptr;
};
