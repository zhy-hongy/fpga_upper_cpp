// sdk_standalone_test_gui/main.cpp
//
// CameraSDK 图形化测试工具
// 通过 Qt Widgets 界面直连 CameraSDK.dll 的公开 C API，
// 支持设置/读取 Sensor 配置、触发配置、批处理配置，并对比显示写入值与回读值。
//
// 用法:
//   sdk_standalone_test_gui [--udp] [ip] [ctrlPort] [dataPort]

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include "photon_camera.h"

// =========================================================================
// 异步回调辅助
// =========================================================================
struct AsyncResult {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    CamError result = CAM_ERR_UNKNOWN;
};

static void CAM_CALL on_async_ack(CamError result, uint16_t /*commandId*/, void* userData) {
    auto* r = static_cast<AsyncResult*>(userData);
    std::lock_guard<std::mutex> lock(r->mutex);
    r->result = result;
    r->done = true;
    r->cv.notify_all();
}

static bool wait_async(AsyncResult& r, int timeoutMs) {
    std::unique_lock<std::mutex> lock(r.mutex);
    return r.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return r.done; });
}

// =========================================================================
// 主窗口
// =========================================================================
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr) : QWidget(parent) {
        setWindowTitle("PhotonCamera SDK 测试工具");
        resize(900, 700);
        build_ui();
        update_ui_state(false);
    }

    ~MainWindow() override {
        if (m_handle) {
            Cam_CloseDevice(m_handle);
            m_handle = nullptr;
        }
        Cam_Finalize();
    }

private slots:
    void on_open() {
        QString ip = m_ipEdit->text().trimmed();
        int ctrlPort = m_ctrlPortSpin->value();
        int dataPort = m_dataPortSpin->value();

        if (ip.isEmpty()) {
            log("错误: IP 地址不能为空", true);
            return;
        }

        Cam_Initialize();
        CameraHandle h = nullptr;
        CamError e = Cam_OpenNetworkDevice(ip.toUtf8().constData(),
                                           static_cast<uint16_t>(ctrlPort),
                                           static_cast<uint16_t>(dataPort), &h);
        if (e == CAM_OK && h) {
            m_handle = h;
            log(QString("设备已打开: %1:%2 (ctrl) / %3 (data)").arg(ip).arg(ctrlPort).arg(dataPort));
            update_ui_state(true);

            // 读取初始设备信息
            DeviceInformation info{};
            if (Cam_GetDeviceInformation(h, &info) == CAM_OK) {
                log(QString("  序列号: %1  状态: %2  FPGA版本: %3")
                    .arg(info.deviceSN).arg(info.status).arg(info.fpgaVersion));
            }
            uint32_t count = 0;
            if (Cam_GetSensorCount(h, &count) == CAM_OK) {
                log(QString("  Sensor 数量: %1").arg(count));
            }
        } else {
            log(QString("打开设备失败: err=%1").arg(e), true);
        }
    }

    void on_close() {
        if (m_handle) {
            Cam_CloseDevice(m_handle);
            m_handle = nullptr;
            log("设备已关闭");
            update_ui_state(false);
        }
    }

    // ── Sensor 配置 ──
    void on_write_sensor() {
        if (!m_handle) return;
        SensorConfig cfg{};
        cfg.exposureTime  = m_expSpin->value();
        cfg.gain          = m_gainSpin->value();
        cfg.exposureDelay = m_delaySpin->value();
        cfg.sampleRate    = m_rateSpin->value();

        CamError e = Cam_SetSensorConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_sensorWritten = cfg;
            update_sensor_written_display();
            log(QString("[Sensor %1] 写入成功: exp=%2 gain=%3 delay=%4 rate=%5")
                .arg(sensor_id()).arg(cfg.exposureTime).arg(cfg.gain)
                .arg(cfg.exposureDelay).arg(cfg.sampleRate));
        } else {
            log(QString("[Sensor %1] 写入失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_read_sensor() {
        if (!m_handle) return;
        SensorConfig cfg{};
        CamError e = Cam_GetSensorConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_sensorRead = cfg;
            update_sensor_read_display();
            log(QString("[Sensor %1] 读取成功: exp=%2 gain=%3 delay=%4 rate=%5")
                .arg(sensor_id()).arg(cfg.exposureTime).arg(cfg.gain)
                .arg(cfg.exposureDelay).arg(cfg.sampleRate));
        } else {
            log(QString("[Sensor %1] 读取失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_write_and_read_sensor() {
        on_write_sensor();
        if (m_handle) {
            // 短暂等待后读取
            QTimer::singleShot(100, this, &MainWindow::on_read_sensor);
        }
    }

    // ── 触发配置 ──
    void on_write_trigger() {
        if (!m_handle) return;
        TriggerConfig cfg{};
        cfg.triggerMode      = static_cast<uint16_t>(m_trigModeSpin->value());
        cfg.encoderType      = static_cast<uint16_t>(m_encTypeSpin->value());
        cfg.encoderInputMode = static_cast<uint16_t>(m_encModeSpin->value());
        cfg.encoderStep      = static_cast<uint32_t>(m_encStepSpin->value());
        cfg.externalEnable   = static_cast<uint16_t>(m_extTrigCheck->isChecked() ? 1 : 0);

        CamError e = Cam_SetTriggerConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_triggerWritten = cfg;
            update_trigger_written_display();
            log(QString("[Sensor %1] 触发配置写入成功").arg(sensor_id()));
        } else {
            log(QString("[Sensor %1] 触发配置写入失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_read_trigger() {
        if (!m_handle) return;
        TriggerConfig cfg{};
        CamError e = Cam_GetTriggerConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_triggerRead = cfg;
            update_trigger_read_display();
            log(QString("[Sensor %1] 触发配置读取成功").arg(sensor_id()));
        } else {
            log(QString("[Sensor %1] 触发配置读取失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_write_and_read_trigger() {
        on_write_trigger();
        if (m_handle) QTimer::singleShot(100, this, &MainWindow::on_read_trigger);
    }

    // ── 批处理配置 ──
    void on_write_batch() {
        if (!m_handle) return;
        BatchConfig cfg{};
        cfg.length = static_cast<uint16_t>(m_batchLenSpin->value());

        CamError e = Cam_SetBatchConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_batchWritten = cfg;
            update_batch_written_display();
            log(QString("[Sensor %1] 批处理配置写入成功: length=%2").arg(sensor_id()).arg(cfg.length));
        } else {
            log(QString("[Sensor %1] 批处理配置写入失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_read_batch() {
        if (!m_handle) return;
        BatchConfig cfg{};
        CamError e = Cam_GetBatchConfig(m_handle, sensor_id(), &cfg);
        if (e == CAM_OK) {
            m_batchRead = cfg;
            update_batch_read_display();
            log(QString("[Sensor %1] 批处理配置读取成功: length=%2").arg(sensor_id()).arg(cfg.length));
        } else {
            log(QString("[Sensor %1] 批处理配置读取失败: err=%2").arg(sensor_id()).arg(e), true);
        }
    }

    void on_write_and_read_batch() {
        on_write_batch();
        if (m_handle) QTimer::singleShot(100, this, &MainWindow::on_read_batch);
    }

    // ── 一键全部写入+回读 ──
    void on_write_and_read_all() {
        on_write_sensor();
        if (!m_handle) return;
        QTimer::singleShot(150, this, [this]() {
            on_read_sensor();
            on_write_trigger();
            QTimer::singleShot(150, this, [this]() {
                on_read_trigger();
                on_write_batch();
                QTimer::singleShot(150, this, [this]() {
                    on_read_batch();
                });
            });
        });
    }

    // ── 采集控制 ──
    void on_start_capture() {
        if (!m_handle) return;
        CamError e = Cam_StartCapture(m_handle, sensor_id());
        log(QString("[Sensor %1] StartCapture -> %2").arg(sensor_id()).arg(e == CAM_OK ? "OK" : "err=" + QString::number(e)));
    }

    void on_stop_capture() {
        if (!m_handle) return;
        CamError e = Cam_StopCapture(m_handle, sensor_id());
        log(QString("[Sensor %1] StopCapture -> %2").arg(sensor_id()).arg(e == CAM_OK ? "OK" : "err=" + QString::number(e)));
    }

    void on_sensor_changed(int /*idx*/) {
        // 切换 Sensor 时清除旧的读写显示
        m_sensorWritten = SensorConfig{};
        m_sensorRead = SensorConfig{};
        m_triggerWritten = TriggerConfig{};
        m_triggerRead = TriggerConfig{};
        m_batchWritten = BatchConfig{};
        m_batchRead = BatchConfig{};
        update_sensor_written_display();
        update_sensor_read_display();
        update_trigger_written_display();
        update_trigger_read_display();
        update_batch_written_display();
        update_batch_read_display();
    }

private:
    // ── 辅助 ──
    uint16_t sensor_id() const {
        return static_cast<uint16_t>(m_sensorCombo->currentData().toInt());
    }

    void log(const QString& msg, bool isError = false) {
        QString prefix = isError ? "[ERROR] " : "";
        m_logEdit->appendPlainText(prefix + msg);
    }

    void update_ui_state(bool opened) {
        m_openBtn->setEnabled(!opened);
        m_closeBtn->setEnabled(opened);
        m_sensorCfgBox->setEnabled(opened);
        m_triggerCfgBox->setEnabled(opened);
        m_batchCfgBox->setEnabled(opened);
        m_captureBox->setEnabled(opened);
        m_ipEdit->setEnabled(!opened);
        m_ctrlPortSpin->setEnabled(!opened);
        m_dataPortSpin->setEnabled(!opened);
    }

    // ── 显示更新 ──
    void update_sensor_written_display() {
        m_sensorWrittenLabel->setText(
            QString("exposureTime = %1 us\n"
                    "gain         = %2\n"
                    "exposureDelay = %3 us\n"
                    "sampleRate   = %4 Hz")
            .arg(m_sensorWritten.exposureTime)
            .arg(m_sensorWritten.gain)
            .arg(m_sensorWritten.exposureDelay)
            .arg(m_sensorWritten.sampleRate));
    }

    void update_sensor_read_display() {
        m_sensorReadLabel->setText(
            QString("exposureTime = %1 us\n"
                    "gain         = %2\n"
                    "exposureDelay = %3 us\n"
                    "sampleRate   = %4 Hz")
            .arg(m_sensorRead.exposureTime)
            .arg(m_sensorRead.gain)
            .arg(m_sensorRead.exposureDelay)
            .arg(m_sensorRead.sampleRate));
        // 颜色标记：匹配绿色，不匹配红色
        update_sensor_match_highlight();
    }

    void update_sensor_match_highlight() {
        bool match = (m_sensorWritten.exposureTime  == m_sensorRead.exposureTime &&
                      m_sensorWritten.gain          == m_sensorRead.gain &&
                      m_sensorWritten.exposureDelay == m_sensorRead.exposureDelay &&
                      m_sensorWritten.sampleRate    == m_sensorRead.sampleRate);
        QString color = match ? "#2e7d32" : "#c62828";
        m_sensorReadLabel->setStyleSheet(
            QString("QLabel { color: %1; font-weight: bold; }").arg(color));
        m_sensorMatchLabel->setText(match ? "? 匹配" : "? 不匹配");
        m_sensorMatchLabel->setStyleSheet(
            QString("QLabel { color: %1; font-weight: bold; font-size: 14px; }").arg(color));
    }

    void update_trigger_written_display() {
        m_triggerWrittenLabel->setText(
            QString("triggerMode      = %1\n"
                    "encoderType      = %2\n"
                    "encoderInputMode = %3\n"
                    "encoderStep      = %4\n"
                    "externalEnable   = %5")
            .arg(m_triggerWritten.triggerMode)
            .arg(m_triggerWritten.encoderType)
            .arg(m_triggerWritten.encoderInputMode)
            .arg(m_triggerWritten.encoderStep)
            .arg(m_triggerWritten.externalEnable));
    }

    void update_trigger_read_display() {
        m_triggerReadLabel->setText(
            QString("triggerMode      = %1\n"
                    "encoderType      = %2\n"
                    "encoderInputMode = %3\n"
                    "encoderStep      = %4\n"
                    "externalEnable   = %5")
            .arg(m_triggerRead.triggerMode)
            .arg(m_triggerRead.encoderType)
            .arg(m_triggerRead.encoderInputMode)
            .arg(m_triggerRead.encoderStep)
            .arg(m_triggerRead.externalEnable));
        bool match = (m_triggerWritten.triggerMode      == m_triggerRead.triggerMode &&
                      m_triggerWritten.encoderType      == m_triggerRead.encoderType &&
                      m_triggerWritten.encoderInputMode == m_triggerRead.encoderInputMode &&
                      m_triggerWritten.encoderStep      == m_triggerRead.encoderStep &&
                      m_triggerWritten.externalEnable   == m_triggerRead.externalEnable);
        QString color = match ? "#2e7d32" : "#c62828";
        m_triggerReadLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(color));
        m_triggerMatchLabel->setText(match ? "? 匹配" : "? 不匹配");
        m_triggerMatchLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
    }

    void update_batch_written_display() {
        m_batchWrittenLabel->setText(QString("length = %1").arg(m_batchWritten.length));
    }

    void update_batch_read_display() {
        m_batchReadLabel->setText(QString("length = %1").arg(m_batchRead.length));
        bool match = (m_batchWritten.length == m_batchRead.length);
        QString color = match ? "#2e7d32" : "#c62828";
        m_batchReadLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(color));
        m_batchMatchLabel->setText(match ? "? 匹配" : "? 不匹配");
        m_batchMatchLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: bold; }").arg(color));
    }

    // ── UI 构建 ──
    void build_ui() {
        auto* mainLayout = new QVBoxLayout(this);

        // ── 连接区域 ──
        auto* connBox = new QGroupBox("连接设置");
        auto* connLayout = new QHBoxLayout(connBox);
        connLayout->addWidget(new QLabel("IP:"));
        m_ipEdit = new QLineEdit("127.0.0.1");
        m_ipEdit->setFixedWidth(120);
        connLayout->addWidget(m_ipEdit);
        connLayout->addWidget(new QLabel("控制端口:"));
        m_ctrlPortSpin = new QSpinBox();
        m_ctrlPortSpin->setRange(1, 65535);
        m_ctrlPortSpin->setValue(5000);
        m_ctrlPortSpin->setFixedWidth(80);
        connLayout->addWidget(m_ctrlPortSpin);
        connLayout->addWidget(new QLabel("数据端口:"));
        m_dataPortSpin = new QSpinBox();
        m_dataPortSpin->setRange(1, 65535);
        m_dataPortSpin->setValue(60000);
        m_dataPortSpin->setFixedWidth(80);
        connLayout->addWidget(m_dataPortSpin);
        connLayout->addStretch();
        m_openBtn = new QPushButton("打开设备");
        m_closeBtn = new QPushButton("关闭设备");
        connLayout->addWidget(m_openBtn);
        connLayout->addWidget(m_closeBtn);
        mainLayout->addWidget(connBox);

        // ── Sensor 选择 ──
        auto* sensorLayout = new QHBoxLayout();
        sensorLayout->addWidget(new QLabel("Sensor:"));
        m_sensorCombo = new QComboBox();
        m_sensorCombo->addItem("Sensor 1", 1);
        m_sensorCombo->addItem("Sensor 2", 2);
        m_sensorCombo->addItem("Sensor 3", 3);
        m_sensorCombo->addItem("Sensor 4", 4);
        sensorLayout->addWidget(m_sensorCombo);
        sensorLayout->addStretch();
        m_allBtn = new QPushButton("一键全部写入+回读");
        sensorLayout->addWidget(m_allBtn);
        mainLayout->addLayout(sensorLayout);

        // ── 配置区域（上半部分：输入控件） ──
        auto* cfgSplitter = new QSplitter(Qt::Vertical);

        // Sensor 配置
        m_sensorCfgBox = new QGroupBox("Sensor 配置");
        auto* sensorCfgLayout = new QGridLayout(m_sensorCfgBox);
        sensorCfgLayout->addWidget(new QLabel("曝光时间 (us):"), 0, 0);
        m_expSpin = new QSpinBox();
        m_expSpin->setRange(0, 9999999);
        m_expSpin->setValue(5000);
        sensorCfgLayout->addWidget(m_expSpin, 0, 1);
        sensorCfgLayout->addWidget(new QLabel("增益:"), 0, 2);
        m_gainSpin = new QSpinBox();
        m_gainSpin->setRange(0, 999999);
        m_gainSpin->setValue(320);
        sensorCfgLayout->addWidget(m_gainSpin, 0, 3);
        sensorCfgLayout->addWidget(new QLabel("曝光延迟 (us):"), 1, 0);
        m_delaySpin = new QSpinBox();
        m_delaySpin->setRange(0, 9999999);
        m_delaySpin->setValue(20);
        sensorCfgLayout->addWidget(m_delaySpin, 1, 1);
        sensorCfgLayout->addWidget(new QLabel("采样率 (Hz):"), 1, 2);
        m_rateSpin = new QSpinBox();
        m_rateSpin->setRange(0, 999999);
        m_rateSpin->setValue(150);
        sensorCfgLayout->addWidget(m_rateSpin, 1, 3);
        auto* sensorBtnLayout = new QHBoxLayout();
        sensorBtnLayout->addStretch();
        auto* sensorWriteBtn = new QPushButton("写入");
        auto* sensorReadBtn = new QPushButton("读取");
        auto* sensorWriteReadBtn = new QPushButton("写入+回读");
        sensorBtnLayout->addWidget(sensorWriteBtn);
        sensorBtnLayout->addWidget(sensorReadBtn);
        sensorBtnLayout->addWidget(sensorWriteReadBtn);
        sensorCfgLayout->addLayout(sensorBtnLayout, 2, 0, 1, 4);
        cfgSplitter->addWidget(m_sensorCfgBox);

        // 触发配置
        m_triggerCfgBox = new QGroupBox("触发配置");
        auto* trigCfgLayout = new QGridLayout(m_triggerCfgBox);
        trigCfgLayout->addWidget(new QLabel("触发模式:"), 0, 0);
        m_trigModeSpin = new QSpinBox();
        m_trigModeSpin->setRange(0, 65535);
        m_trigModeSpin->setValue(2);
        trigCfgLayout->addWidget(m_trigModeSpin, 0, 1);
        trigCfgLayout->addWidget(new QLabel("编码器类型:"), 0, 2);
        m_encTypeSpin = new QSpinBox();
        m_encTypeSpin->setRange(0, 65535);
        m_encTypeSpin->setValue(1);
        trigCfgLayout->addWidget(m_encTypeSpin, 0, 3);
        trigCfgLayout->addWidget(new QLabel("编码器模式:"), 1, 0);
        m_encModeSpin = new QSpinBox();
        m_encModeSpin->setRange(0, 65535);
        m_encModeSpin->setValue(2);
        trigCfgLayout->addWidget(m_encModeSpin, 1, 1);
        trigCfgLayout->addWidget(new QLabel("编码器步长:"), 1, 2);
        m_encStepSpin = new QSpinBox();
        m_encStepSpin->setRange(0, 999999999);
        m_encStepSpin->setValue(500);
        trigCfgLayout->addWidget(m_encStepSpin, 1, 3);
        m_extTrigCheck = new QCheckBox("外部触发使能");
        m_extTrigCheck->setChecked(true);
        trigCfgLayout->addWidget(m_extTrigCheck, 2, 0, 1, 2);
        auto* trigBtnLayout = new QHBoxLayout();
        trigBtnLayout->addStretch();
        auto* trigWriteBtn = new QPushButton("写入");
        auto* trigReadBtn = new QPushButton("读取");
        auto* trigWriteReadBtn = new QPushButton("写入+回读");
        trigBtnLayout->addWidget(trigWriteBtn);
        trigBtnLayout->addWidget(trigReadBtn);
        trigBtnLayout->addWidget(trigWriteReadBtn);
        trigCfgLayout->addLayout(trigBtnLayout, 3, 0, 1, 4);
        cfgSplitter->addWidget(m_triggerCfgBox);

        // 批处理配置
        m_batchCfgBox = new QGroupBox("批处理配置");
        auto* batchCfgLayout = new QGridLayout(m_batchCfgBox);
        batchCfgLayout->addWidget(new QLabel("批处理行数:"), 0, 0);
        m_batchLenSpin = new QSpinBox();
        m_batchLenSpin->setRange(0, 65535);
        m_batchLenSpin->setValue(32);
        batchCfgLayout->addWidget(m_batchLenSpin, 0, 1);
        batchCfgLayout->addWidget(new QLabel(""), 0, 2);
        batchCfgLayout->addWidget(new QLabel(""), 0, 3);
        auto* batchBtnLayout = new QHBoxLayout();
        batchBtnLayout->addStretch();
        auto* batchWriteBtn = new QPushButton("写入");
        auto* batchReadBtn = new QPushButton("读取");
        auto* batchWriteReadBtn = new QPushButton("写入+回读");
        batchBtnLayout->addWidget(batchWriteBtn);
        batchBtnLayout->addWidget(batchReadBtn);
        batchBtnLayout->addWidget(batchWriteReadBtn);
        batchCfgLayout->addLayout(batchBtnLayout, 1, 0, 1, 4);
        cfgSplitter->addWidget(m_batchCfgBox);

        mainLayout->addWidget(cfgSplitter);

        // ── 采集控制 ──
        m_captureBox = new QGroupBox("采集控制");
        auto* capLayout = new QHBoxLayout(m_captureBox);
        auto* startCapBtn = new QPushButton("开始采集");
        auto* stopCapBtn = new QPushButton("停止采集");
        capLayout->addWidget(startCapBtn);
        capLayout->addWidget(stopCapBtn);
        capLayout->addStretch();
        mainLayout->addWidget(m_captureBox);

        // ── 对比显示区域（写入值 vs 回读值） ──
        auto* compareBox = new QGroupBox("写入值 vs 回读值对比");
        auto* compareLayout = new QGridLayout(compareBox);

        // Sensor 对比
        auto* sensorCompareLabel = new QLabel("<b>Sensor 配置</b>");
        sensorCompareLabel->setStyleSheet("font-size: 13px;");
        compareLayout->addWidget(sensorCompareLabel, 0, 0, 1, 3);
        m_sensorMatchLabel = new QLabel("");
        m_sensorMatchLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
        compareLayout->addWidget(m_sensorMatchLabel, 0, 3);

        compareLayout->addWidget(new QLabel("<b>写入值:</b>"), 1, 0);
        m_sensorWrittenLabel = new QLabel("(未写入)");
        m_sensorWrittenLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_sensorWrittenLabel, 1, 1);

        compareLayout->addWidget(new QLabel("<b>回读值:</b>"), 1, 2);
        m_sensorReadLabel = new QLabel("(未读取)");
        m_sensorReadLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_sensorReadLabel, 1, 3);

        // Trigger 对比
        auto* trigCompareLabel = new QLabel("<b>触发配置</b>");
        trigCompareLabel->setStyleSheet("font-size: 13px;");
        compareLayout->addWidget(trigCompareLabel, 2, 0, 1, 3);
        m_triggerMatchLabel = new QLabel("");
        compareLayout->addWidget(m_triggerMatchLabel, 2, 3);

        compareLayout->addWidget(new QLabel("<b>写入值:</b>"), 3, 0);
        m_triggerWrittenLabel = new QLabel("(未写入)");
        m_triggerWrittenLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_triggerWrittenLabel, 3, 1);

        compareLayout->addWidget(new QLabel("<b>回读值:</b>"), 3, 2);
        m_triggerReadLabel = new QLabel("(未读取)");
        m_triggerReadLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_triggerReadLabel, 3, 3);

        // Batch 对比
        auto* batchCompareLabel = new QLabel("<b>批处理配置</b>");
        batchCompareLabel->setStyleSheet("font-size: 13px;");
        compareLayout->addWidget(batchCompareLabel, 4, 0, 1, 3);
        m_batchMatchLabel = new QLabel("");
        compareLayout->addWidget(m_batchMatchLabel, 4, 3);

        compareLayout->addWidget(new QLabel("<b>写入值:</b>"), 5, 0);
        m_batchWrittenLabel = new QLabel("(未写入)");
        m_batchWrittenLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_batchWrittenLabel, 5, 1);

        compareLayout->addWidget(new QLabel("<b>回读值:</b>"), 5, 2);
        m_batchReadLabel = new QLabel("(未读取)");
        m_batchReadLabel->setStyleSheet("QLabel { font-family: monospace; }");
        compareLayout->addWidget(m_batchReadLabel, 5, 3);

        mainLayout->addWidget(compareBox);

        // ── 日志区域 ──
        m_logEdit = new QPlainTextEdit();
        m_logEdit->setReadOnly(true);
        m_logEdit->setMaximumBlockCount(500);
        m_logEdit->setStyleSheet("QPlainTextEdit { font-family: Consolas, monospace; font-size: 12px; }");
        mainLayout->addWidget(m_logEdit);

        // ── 信号连接 ──
        connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::on_open);
        connect(m_closeBtn, &QPushButton::clicked, this, &MainWindow::on_close);
        connect(sensorWriteBtn, &QPushButton::clicked, this, &MainWindow::on_write_sensor);
        connect(sensorReadBtn, &QPushButton::clicked, this, &MainWindow::on_read_sensor);
        connect(sensorWriteReadBtn, &QPushButton::clicked, this, &MainWindow::on_write_and_read_sensor);
        connect(trigWriteBtn, &QPushButton::clicked, this, &MainWindow::on_write_trigger);
        connect(trigReadBtn, &QPushButton::clicked, this, &MainWindow::on_read_trigger);
        connect(trigWriteReadBtn, &QPushButton::clicked, this, &MainWindow::on_write_and_read_trigger);
        connect(batchWriteBtn, &QPushButton::clicked, this, &MainWindow::on_write_batch);
        connect(batchReadBtn, &QPushButton::clicked, this, &MainWindow::on_read_batch);
        connect(batchWriteReadBtn, &QPushButton::clicked, this, &MainWindow::on_write_and_read_batch);
        connect(m_allBtn, &QPushButton::clicked, this, &MainWindow::on_write_and_read_all);
        connect(startCapBtn, &QPushButton::clicked, this, &MainWindow::on_start_capture);
        connect(stopCapBtn, &QPushButton::clicked, this, &MainWindow::on_stop_capture);
        connect(m_sensorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::on_sensor_changed);

        log("PhotonCamera SDK 测试工具已启动");
        log(QString::fromUtf8("请设置连接参数，点击 [打开设备] 开始测试"));
    }

private:
    CameraHandle m_handle = nullptr;

    // 连接控件
    QLineEdit* m_ipEdit = nullptr;
    QSpinBox* m_ctrlPortSpin = nullptr;
    QSpinBox* m_dataPortSpin = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    // Sensor 选择
    QComboBox* m_sensorCombo = nullptr;
    QPushButton* m_allBtn = nullptr;

    // 配置分组
    QGroupBox* m_sensorCfgBox = nullptr;
    QGroupBox* m_triggerCfgBox = nullptr;
    QGroupBox* m_batchCfgBox = nullptr;
    QGroupBox* m_captureBox = nullptr;

    // Sensor 配置输入
    QSpinBox* m_expSpin = nullptr;
    QSpinBox* m_gainSpin = nullptr;
    QSpinBox* m_delaySpin = nullptr;
    QSpinBox* m_rateSpin = nullptr;

    // 触发配置输入
    QSpinBox* m_trigModeSpin = nullptr;
    QSpinBox* m_encTypeSpin = nullptr;
    QSpinBox* m_encModeSpin = nullptr;
    QSpinBox* m_encStepSpin = nullptr;
    QCheckBox* m_extTrigCheck = nullptr;

    // 批处理配置输入
    QSpinBox* m_batchLenSpin = nullptr;

    // 对比显示 (Sensor)
    QLabel* m_sensorWrittenLabel = nullptr;
    QLabel* m_sensorReadLabel = nullptr;
    QLabel* m_sensorMatchLabel = nullptr;

    // 对比显示 (Trigger)
    QLabel* m_triggerWrittenLabel = nullptr;
    QLabel* m_triggerReadLabel = nullptr;
    QLabel* m_triggerMatchLabel = nullptr;

    // 对比显示 (Batch)
    QLabel* m_batchWrittenLabel = nullptr;
    QLabel* m_batchReadLabel = nullptr;
    QLabel* m_batchMatchLabel = nullptr;

    // 日志
    QPlainTextEdit* m_logEdit = nullptr;

    // 缓存值
    SensorConfig  m_sensorWritten{};
    SensorConfig  m_sensorRead{};
    TriggerConfig m_triggerWritten{};
    TriggerConfig m_triggerRead{};
    BatchConfig   m_batchWritten{};
    BatchConfig   m_batchRead{};
};

// =========================================================================
// main
// =========================================================================
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    MainWindow w;
    w.show();

    return app.exec();
}

#include "main.moc"