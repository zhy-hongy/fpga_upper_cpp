#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QMap>

class QTimer;
class QTabWidget;
class QButtonGroup;
class QSplitter;
class QLabel;
class QPushButton;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QLineEdit;
class QProgressBar;
class QCheckBox;
class QRadioButton;
class QGroupBox;
class QWidget;
class QVBoxLayout;
class QHBoxLayout;
class QGraphicsOpacityEffect;

#include "utils/config.h"

class ImageView;
class StatusBarWidget;
class ProtocolHandler;
struct CameraFrame;
struct ControlFrame;
struct Config;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const Config& config, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Display mode toggles
    void on_image_toggled(bool checked);
    void on_contour_toggled(bool checked);
    void on_centerline_toggled(bool checked);
    void on_3d_toggled(bool checked);
    void on_measure_toggled_v(bool checked);
    void on_measure_toggled_h(bool checked);

    // Layout
    void on_layout_changed(int id, bool checked);

    // Zoom
    void on_zoom_row_in();
    void on_zoom_row_out();
    void on_zoom_col_in();
    void on_zoom_col_out();
    void on_zoom_fit();

    // Network
    void on_net_connect();
    void on_net_disconnect();

    // Camera control
    void on_exp_changed();
    void on_gain_changed();
    void on_apply_params();
    void on_read_params();
    void on_read_exp_done(const ControlFrame& frame);
    void on_read_gain_done(const ControlFrame& frame);
    void on_send_exp_delay();

    // Trigger
    void on_trigger_mode_changed(int index);

    // Encoder
    void on_encoder_type_changed(int index);
    void on_encoder_mode_changed(int index);
    void on_encoder_interval_changed(int value);

    // External trigger
    void on_external_trigger_toggled(bool checked);

    // Batch
    void on_batch_start();
    void on_batch_count_changed(int count);
    void on_batch_stop();
    void on_batch_reset();
    void on_batch_complete();

    // Firmware
    void on_browse_firmware();
    void on_upgrade_start();
    void on_upgrade_request_done(const ControlFrame& frame);
    void on_upgrade_control_done(const ControlFrame& frame);
    void send_next_chunk();
    void on_chunk_done(const ControlFrame& frame);
    void on_chunk_error();
    void on_upgrade_result(const ControlFrame& frame);
    void on_upgrade_error();

    // Protocol signals
    void on_connected();
    void on_disconnected();
    void on_control_response(const ControlFrame& frame);
    void on_frame_info(int fps, int total, int dropped);
    void on_resolution_changed(int sensor_id, int w, int h);
    void on_status_message(const QString& msg);
    void on_error_occurred(const QString& msg);

    // Misc
    void on_tab_changed(int index);
    void on_save_image();
    void on_open_protocol_analyzer();
    void on_sample_rate_changed(const QString& rate);
    void on_load_test();

private:
    // UI construction
    void init_ui();
    void connect_signals();
    void apply_display_mode();
    void set_layout(int count);
    void set_batch_locked(bool locked);
    void set_upgrade_ui_enabled(bool enabled);
    void reset_upgrade_state();

    // Tab builders
    QWidget* create_tab_page(void (MainWindow::*builder)(QVBoxLayout*));
    void build_trigger_tab(QVBoxLayout* parent);
    void build_image_tab(QVBoxLayout* parent);
    void build_system_tab(QVBoxLayout* parent);
    void build_camera_selector_group(QVBoxLayout* parent);

    // Individual group builders
    void build_trigger_mode_group(QVBoxLayout* parent);
    void build_sample_rate_group(QVBoxLayout* parent);
    void build_3d_interval_group(QVBoxLayout* parent);
    void build_work_mode_group(QVBoxLayout* parent);
    void build_cam_group(QVBoxLayout* parent);
    void build_exposure_delay_group(QVBoxLayout* parent);
    void build_measure_range_group(QVBoxLayout* parent);
    void build_image_enhance_group(QVBoxLayout* parent);
    void build_image_display_group(QVBoxLayout* parent);
    void build_image_roi_group(QVBoxLayout* parent);
    void build_net_group(QVBoxLayout* parent);
    void build_device_ip_group(QVBoxLayout* parent);
    void build_firmware_upgrade_group(QVBoxLayout* parent);
    void build_system_info_group(QVBoxLayout* parent);
    void build_system_sync_group(QVBoxLayout* parent);
    void build_status_group(QVBoxLayout* parent);
    void build_protocol_analyzer_group(QVBoxLayout* parent);

    // Polling
    void poll_frame();
    QVector<int> visible_camera_indices() const;

    // Helpers
    ImageView* active_view();
    QVector<int> target_sensor_ids() const;
    QString camera_label() const;
    static QStringList get_local_ips();
    void refresh_local_ips(QComboBox* combo);
    void log(const QString& msg);
    void log_error(const QString& msg);

    // ── State ──
    Config m_config;
    ProtocolHandler* m_protocol = nullptr;
    QMap<int, CameraFrame> m_camera_frames;

    int m_selected_camera = 1;
    int m_current_layout = 1;
    int m_total_frames = 0;
    int m_dropped_packets = 0;
    bool m_image_logged = false;

    // Firmware upgrade state
    QString m_firmware_path;
    QVector<QByteArray> m_firmware_chunks;
    int m_firmware_chunk_index = 0;
    int m_firmware_total_chunks = 0;
    bool m_upgrade_in_progress = false;

    // ── Widgets: Display ──
    ImageView* m_image_widget = nullptr;
    QVector<ImageView*> m_image_views;
    QWidget* m_display_container = nullptr;
    QVBoxLayout* m_display_layout = nullptr;

    // Mode buttons
    QPushButton* m_btn_show_image = nullptr;
    QPushButton* m_btn_show_contour = nullptr;
    QPushButton* m_btn_show_3d = nullptr;
    QPushButton* m_btn_show_centerline = nullptr;
    QPushButton* m_btn_save = nullptr;

    // Measure buttons
    QPushButton* m_btn_measure_v = nullptr;
    QPushButton* m_btn_measure_h = nullptr;

    // Layout buttons
    QPushButton* m_btn_layout_1 = nullptr;
    QPushButton* m_btn_layout_2 = nullptr;
    QPushButton* m_btn_layout_4 = nullptr;
    QButtonGroup* m_layout_group = nullptr;

    // Zoom buttons
    QPushButton* m_btn_zoom_row_in = nullptr;
    QPushButton* m_btn_zoom_row_out = nullptr;
    QPushButton* m_btn_zoom_fit = nullptr;
    QPushButton* m_btn_zoom_col_in = nullptr;
    QPushButton* m_btn_zoom_col_out = nullptr;

    // ── Widgets: Right panel ──
    QGroupBox* m_cam_selector_group = nullptr;
    QButtonGroup* m_camera_group = nullptr;
    QTabWidget* m_tabs_right = nullptr;

    // Trigger tab
    QGroupBox* m_grp_trigger_mode = nullptr;
    QComboBox* m_combo_trigger_mode = nullptr;
    QWidget* m_encoder_widget = nullptr;
    QComboBox* m_combo_encoder_type = nullptr;
    QComboBox* m_combo_encoder_mode = nullptr;
    QSpinBox* m_spin_encoder_subdivision = nullptr;
    QGroupBox* m_grp_sample_rate = nullptr;
    QComboBox* m_combo_sample_rate = nullptr;
    QGroupBox* m_grp_3d_interval = nullptr;
    QDoubleSpinBox* m_spin_3d_interval = nullptr;
    QGroupBox* m_grp_work_mode = nullptr;
    QSpinBox* m_spin_batch_count = nullptr;
    QCheckBox* m_chk_external_trigger = nullptr;
    QPushButton* m_btn_batch_start = nullptr;
    QPushButton* m_btn_batch_stop = nullptr;
    QPushButton* m_btn_batch_reset = nullptr;

    // Image tab
    QSlider* m_exp_slider = nullptr;
    QSpinBox* m_exp_spin = nullptr;
    QSlider* m_gain_slider = nullptr;
    QSpinBox* m_gain_spin = nullptr;
    QPushButton* m_btn_apply = nullptr;
    QPushButton* m_btn_read = nullptr;
    QGroupBox* m_exp_delay_group = nullptr;
    QVector<QSpinBox*> m_exp_delay_spins;
    QPushButton* m_btn_exp_delay_send = nullptr;

    // System tab
    QLineEdit* m_edit_fpga = nullptr;
    QPushButton* m_btn_refresh_fpga = nullptr;
    QComboBox* m_combo_host = nullptr;
    QSpinBox* m_spin_fpga_port = nullptr;
    QSpinBox* m_spin_local_port = nullptr;
    QPushButton* m_btn_connect = nullptr;
    QPushButton* m_btn_disconnect = nullptr;
    QLineEdit* m_edit_device_ip = nullptr;
    QLineEdit* m_edit_device_mask = nullptr;
    QLineEdit* m_edit_device_gateway = nullptr;
    QLineEdit* m_edit_device_mac = nullptr;
    QComboBox* m_combo_local_ip = nullptr;
    QLineEdit* m_edit_firmware_path = nullptr;
    QPushButton* m_btn_browse_firmware = nullptr;
    QProgressBar* m_progress_firmware = nullptr;
    QLabel* m_lbl_upgrade_status = nullptr;
    QPushButton* m_btn_upgrade_start = nullptr;
    QLabel* m_lbl_fw_version = nullptr;
    QLabel* m_lbl_fps_val = nullptr;
    QLabel* m_lbl_frame_val = nullptr;
    QLabel* m_lbl_dropped_val = nullptr;
    QLabel* m_lbl_pkt_val = nullptr;
    QLabel* m_lbl_res_val = nullptr;
    QPushButton* m_btn_protocol_analyzer = nullptr;

    // ── Widgets: Status bar ──
    StatusBarWidget* m_status_widget = nullptr;

    // ── Timer ──
    QTimer* m_display_timer = nullptr;
};

#endif // MAIN_WINDOW_H
