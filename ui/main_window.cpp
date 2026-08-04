#include "main_window.h"
#include "ui/widgets.h"
#include "ui/protocol_analyzer.h"
#include "sdk_test/sdk_test.h"
#include "communication/protocol_handler.h"
#include "protocol/camera_frame.h"
#include "protocol/control_frame.h"
#include "protocol/upgrade_frame.h"
#include "utils/logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QFormLayout>
#include <QButtonGroup>
#include <QTabWidget>
#include <QScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QRadioButton>
#include <QGridLayout>
#include <QFrame>
#include <QProgressBar>
#include <QSplitter>
#include <QSizePolicy>
#include <QGraphicsOpacityEffect>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QDir>
#include <QDateTime>
#include <QCloseEvent>
#include <QStatusBar>
#include <QScrollBar>
#include <QFile>
#include <algorithm>
#include <opencv2/imgcodecs.hpp>

// ══════════════════════════════════════════════════
// Dark industrial QSS theme
// ══════════════════════════════════════════════════

static const char* DARK_QSS = R"(
QMainWindow, QWidget {
    background-color: #1A1A1A;
    color: #C0C0C0;
    font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
    font-size: 13px;
}
QGroupBox {
    background-color: #242424;
    border: 1px solid #3A3A3A;
    border-radius: 6px;
    margin-top: 18px;
    font-weight: bold;
    color: #A0A0A0;
    padding: 18px 10px 10px 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 2px 8px;
    color: #00A896;
    font-size: 13px;
}
QPushButton {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #C0C0C0;
    padding: 5px 14px;
    border-radius: 4px;
    min-height: 22px;
    font-size: 12px;
}
QPushButton:hover {
    border-color: #00A896;
    color: #00FFCC;
}
QPushButton:pressed {
    background-color: #00A896;
    color: #FFFFFF;
}
QPushButton:disabled {
    background-color: #1A1A1A;
    color: #555555;
    border-color: #2A2A2A;
}
QTabWidget::pane {
    border: 1px solid #3A3A3A;
    background-color: #1F1F1F;
    border-radius: 6px;
    top: -1px;
}
QTabBar::tab {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #A0A0A0;
    padding: 6px 14px;
    min-width: 60px;
}
QTabBar::tab:selected {
    background-color: #004D40;
    border-color: #00A896;
    color: #00FFCC;
    font-weight: bold;
}
QTabBar::tab:hover {
    border-color: #00A896;
}
QComboBox {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #C0C0C0;
    padding: 3px 6px;
    border-radius: 3px;
    min-height: 22px;
}
QComboBox:hover {
    border-color: #00A896;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    border: none;
    border-left: 1px solid #3A3A3A;
    width: 22px;
    background-color: #2E2E2E;
    border-top-right-radius: 3px;
    border-bottom-right-radius: 3px;
}
QComboBox::down-arrow {
    image: url(:/assets/arrow_down.svg);
    width: 10px;
    height: 6px;
}
QComboBox QAbstractItemView {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    outline: none;
}
QComboBox QAbstractItemView::item {
    color: #C0C0C0;
    padding: 4px 8px;
    min-height: 20px;
}
QComboBox QAbstractItemView::item:hover,
QComboBox QAbstractItemView::item:selected {
    background-color: #004D40;
    color: #00FFCC;
}
QCheckBox, QRadioButton {
    color: #A0A0A0;
    spacing: 6px;
}
QRadioButton::indicator {
    border: 1px solid #4A4A4A;
    background-color: #1E1E1E;
    width: 14px;
    height: 14px;
    border-radius: 7px;
}
QRadioButton::indicator:hover {
    border-color: #00A896;
}
QRadioButton::indicator:checked {
    background-color: #00A896;
    border: 2px solid #00FFCC;
}
QCheckBox::indicator {
    border: 1px solid #4A4A4A;
    background-color: #1E1E1E;
    width: 14px;
    height: 14px;
    border-radius: 3px;
}
QCheckBox::indicator:hover {
    border-color: #00A896;
}
QCheckBox::indicator:checked {
    background-color: #00A896;
    border: 1px solid #00FFCC;
}
QScrollArea {
    border: none;
    background-color: transparent;
}
QPushButton#mode_btn {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #A0A0A0;
    padding: 6px 20px;
    border-radius: 4px;
    font-size: 12px;
    min-height: 26px;
}
QPushButton#mode_btn:hover {
    border-color: #00A896;
    color: #C0C0C0;
}
QPushButton#mode_btn:checked {
    background-color: #004D40;
    border-color: #00A896;
    color: #00FFCC;
    font-weight: bold;
}
QPushButton#btn_start {
    background-color: #004D40;
    border-color: #00A896;
    color: #00FFCC;
}
QPushButton#btn_start:hover {
    background-color: #00695C;
}
QPushButton#btn_stop {
    background-color: #4A0020;
    border-color: #A80040;
    color: #FF6699;
}
QPushButton#btn_stop:hover {
    background-color: #6A0030;
}
QLabel {
    color: #A0A0A0;
    font-size: 12px;
}
QLabel#val_label {
    color: #00FFCC;
    font-weight: bold;
    font-size: 13px;
}
QLineEdit {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #C0C0C0;
    padding: 3px 6px;
    border-radius: 3px;
    font-size: 12px;
}
QLineEdit:focus {
    border-color: #00A896;
}
QSpinBox, QDoubleSpinBox {
    background-color: #2A2A2A;
    border: 1px solid #3A3A3A;
    color: #00FFCC;
    padding: 3px 6px;
    border-radius: 3px;
    font-size: 12px;
    min-width: 70px;
}
QSpinBox:focus, QDoubleSpinBox:focus {
    border-color: #00A896;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    border: none;
    border-left: 1px solid #3A3A3A;
    border-bottom: 1px solid #3A3A3A;
    background-color: #2E2E2E;
    width: 18px;
    border-top-right-radius: 3px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover {
    background-color: #3A3A3A;
}
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed {
    background-color: #004D40;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    border: none;
    border-left: 1px solid #3A3A3A;
    background-color: #2E2E2E;
    width: 18px;
    border-bottom-right-radius: 3px;
}
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: #3A3A3A;
}
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
    background-color: #004D40;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url(:/assets/arrow_up.svg);
    width: 10px;
    height: 6px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url(:/assets/arrow_down.svg);
    width: 10px;
    height: 6px;
}
QSlider::groove:horizontal {
    height: 4px;
    background: #3A3A3A;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #00A896;
    width: 12px;
    height: 12px;
    margin: -4px 0;
    border-radius: 6px;
}
QSlider::handle:horizontal:hover {
    background: #00FFCC;
    width: 14px;
    height: 14px;
}
QTextEdit {
    background-color: #1A1A1A;
    color: #A0A0A0;
    border: 1px solid #3A3A3A;
    border-radius: 4px;
    font-family: "Consolas", "Courier New", monospace;
    font-size: 11px;
    padding: 4px;
}
QStatusBar {
    background-color: #0D0D0D;
    border-top: 1px solid #2A2A2A;
    color: #A0A0A0;
    font-size: 12px;
}
QScrollBar:horizontal {
    background: #1A1A1A;
    height: 10px;
    border: none;
}
QScrollBar::handle:horizontal {
    background: #3A3A3A;
    min-width: 30px;
    border-radius: 5px;
}
QScrollBar::handle:horizontal:hover {
    background: #00A896;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: none;
}
QScrollBar:vertical {
    background: #1A1A1A;
    width: 10px;
    border: none;
}
QScrollBar::handle:vertical {
    background: #3A3A3A;
    min-height: 30px;
    border-radius: 5px;
}
QScrollBar::handle:vertical:hover {
    background: #00A896;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
}
)";

// ══════════════════════════════════════════════════
// MainWindow
// ══════════════════════════════════════════════════

MainWindow::MainWindow(const Config& config, QWidget* parent)
    : QMainWindow(parent), m_config(config) {

    m_protocol = new ProtocolHandler(m_config, DEV_SENSOR1, this);

    for (int sid = 1; sid <= 4; sid++) {
        m_camera_frames[sid] = CameraFrame();
        m_camera_frames[sid].sensor_id = sid;
    }

    init_ui();
    connect_signals();

    m_display_timer = new QTimer(this);
    connect(m_display_timer, &QTimer::timeout, this, &MainWindow::poll_frame);
    m_display_timer->start(10);
}

MainWindow::~MainWindow() {
    // Stop all timers first
    if (m_display_timer) {
        m_display_timer->stop();
        disconnect(m_display_timer, nullptr, this, nullptr);
    }

    // Disconnect all signals from protocol handler to prevent callbacks during destruction
    if (m_protocol) {
        disconnect(m_protocol, nullptr, this, nullptr);
        // Stop all networking activities
        m_protocol->stop_all();
    }

    // Disconnect all remaining connections to avoid signal emissions during destruction
    disconnect(this, nullptr, nullptr, nullptr);
}

// ══════════════════════════════════════════════════
// UI Construction
// ══════════════════════════════════════════════════

void MainWindow::init_ui() {
    setWindowTitle(m_config.window_title);
    resize(1600, 1000);
    setStyleSheet(DARK_QSS);

    auto* central = new QWidget();
    setCentralWidget(central);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(2);
    splitter->setStyleSheet("QSplitter::handle { background-color: #3A3A3A; }");
    splitter->setChildrenCollapsible(false);

    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->addWidget(splitter);

    // ── Left panel ──
    auto* left_panel = new QWidget();
    left_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* left = new QVBoxLayout(left_panel);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(6);

    // Mode button row
    auto* mode_row = new QHBoxLayout();
    mode_row->setSpacing(8);

    m_btn_show_image = new QPushButton("图像");
    m_btn_show_image->setObjectName("mode_btn");
    m_btn_show_image->setCheckable(true);
    m_btn_show_image->setChecked(true);

    m_btn_show_contour = new QPushButton("轮廓");
    m_btn_show_contour->setObjectName("mode_btn");
    m_btn_show_contour->setCheckable(true);

    m_btn_show_3d = new QPushButton("3D");
    m_btn_show_3d->setObjectName("mode_btn");
    m_btn_show_3d->setCheckable(true);

    mode_row->addWidget(m_btn_show_image);
    mode_row->addWidget(m_btn_show_contour);
    mode_row->addWidget(m_btn_show_3d);
    mode_row->addStretch();

    m_btn_save = new QPushButton("保存图片");
    m_btn_save->setObjectName("mode_btn");
    mode_row->addWidget(m_btn_save);
    left->addLayout(mode_row);

    // Toolbar row: layout + centerline + measure + zoom
    auto* layout_row = new QHBoxLayout();
    layout_row->setSpacing(6);

    const char* btn_style = R"(
        QPushButton { background-color: #2A2A2A; border: 1px solid #3A3A3A; color: #A0A0A0; border-radius: 4px; font-size: 18px; font-weight: bold; padding: 0px; }
        QPushButton:hover { border-color: #00A896; color: #00FFCC; background-color: #333333; }
        QPushButton:checked { border-color: #00A896; color: #00FFCC; background-color: #1A3A3A; }
    )";

    m_layout_group = new QButtonGroup(this);
    m_layout_group->setExclusive(true);

    m_btn_layout_1 = new QPushButton(QString::fromUtf8("□")); // □
    m_btn_layout_1->setCheckable(true);
    m_btn_layout_1->setChecked(true);
    m_btn_layout_1->setFixedSize(32, 32);
    m_btn_layout_1->setStyleSheet(btn_style);
    m_btn_layout_1->setToolTip("单画面");
    m_layout_group->addButton(m_btn_layout_1, 1);

    m_btn_layout_2 = new QPushButton(QString::fromUtf8("◫")); // ◫
    m_btn_layout_2->setCheckable(true);
    m_btn_layout_2->setFixedSize(32, 32);
    m_btn_layout_2->setStyleSheet(btn_style);
    m_btn_layout_2->setToolTip("双画面");
    m_layout_group->addButton(m_btn_layout_2, 2);

    m_btn_layout_4 = new QPushButton(QString::fromUtf8("⊞")); // ⊞
    m_btn_layout_4->setCheckable(true);
    m_btn_layout_4->setFixedSize(32, 32);
    m_btn_layout_4->setStyleSheet(btn_style);
    m_btn_layout_4->setToolTip("四画面");
    m_layout_group->addButton(m_btn_layout_4, 4);

    layout_row->addWidget(m_btn_layout_1);
    layout_row->addWidget(m_btn_layout_2);
    layout_row->addWidget(m_btn_layout_4);
    layout_row->addSpacing(12);

    // Centerline button
    m_btn_show_centerline = new QPushButton();
    m_btn_show_centerline->setObjectName("mode_btn");
    m_btn_show_centerline->setCheckable(true);
    m_btn_show_centerline->setToolTip("中心线");
    m_btn_show_centerline->setText("中心线");  // 添加文本，明确显示功能

    // 尝试从 assets 加载图标
    QString assetsDir = QCoreApplication::applicationDirPath() + "/assets";
    QString clIcon = assetsDir + "/center_line.svg";
    if (QFile::exists(clIcon)) {
        m_btn_show_centerline->setIcon(QIcon(clIcon));
        m_btn_show_centerline->setIconSize(QSize(40, 40));
    }
    else {
        // 若图标不存在，可调整文本样式或保持纯文本（无需额外操作）
        // 可根据需要设置字体或样式，例如：
        // m_btn_show_centerline->setStyleSheet("font-weight: bold;");
    }

    layout_row->addWidget(m_btn_show_centerline);
    layout_row->addSpacing(12);

    // Measure buttons
    m_btn_measure_v = new QPushButton();
    m_btn_measure_v->setObjectName("mode_btn");
    m_btn_measure_v->setCheckable(true);
    m_btn_measure_v->setToolTip("竖线测量");
    QString wIcon = assetsDir + "/width.png";
    if (QFile::exists(wIcon))
        m_btn_measure_v->setIcon(QIcon(wIcon));
    m_btn_measure_v->setIconSize(QSize(28, 28));
    layout_row->addWidget(m_btn_measure_v);

    m_btn_measure_h = new QPushButton();
    m_btn_measure_h->setObjectName("mode_btn");
    m_btn_measure_h->setCheckable(true);
    m_btn_measure_h->setToolTip("横线测量");
    QString hIcon = assetsDir + "/height.png";
    if (QFile::exists(hIcon))
        m_btn_measure_h->setIcon(QIcon(hIcon));
    m_btn_measure_h->setIconSize(QSize(28, 28));
    layout_row->addWidget(m_btn_measure_h);

    layout_row->addStretch();

    const char* zoom_style = R"(
        QPushButton { background-color: #2A2A2A; border: 1px solid #3A3A3A; color: #A0A0A0; border-radius: 4px; font-size: 12px; font-weight: bold; padding: 0px; }
        QPushButton:hover { border-color: #00A896; color: #00FFCC; background-color: #333333; }
    )";

    m_btn_zoom_row_in = new QPushButton("行+");
    m_btn_zoom_row_in->setFixedSize(40, 28);
    m_btn_zoom_row_in->setStyleSheet(zoom_style);
    m_btn_zoom_row_in->setToolTip("行放大");
    layout_row->addWidget(m_btn_zoom_row_in);

    m_btn_zoom_row_out = new QPushButton("行－");
    m_btn_zoom_row_out->setFixedSize(40, 28);
    m_btn_zoom_row_out->setStyleSheet(zoom_style);
    m_btn_zoom_row_out->setToolTip("行缩小");
    layout_row->addWidget(m_btn_zoom_row_out);

    m_btn_zoom_fit = new QPushButton(QString::fromUtf8("⟲")); // ⟲
    m_btn_zoom_fit->setFixedSize(28, 28);
    m_btn_zoom_fit->setStyleSheet(zoom_style);
    m_btn_zoom_fit->setToolTip("适应窗口(原图)");
    layout_row->addWidget(m_btn_zoom_fit);

    m_btn_zoom_col_in = new QPushButton("列+");
    m_btn_zoom_col_in->setFixedSize(40, 28);
    m_btn_zoom_col_in->setStyleSheet(zoom_style);
    m_btn_zoom_col_in->setToolTip("列放大");
    layout_row->addWidget(m_btn_zoom_col_in);

    m_btn_zoom_col_out = new QPushButton("列－");
    m_btn_zoom_col_out->setFixedSize(40, 28);
    m_btn_zoom_col_out->setStyleSheet(zoom_style);
    m_btn_zoom_col_out->setToolTip("列缩小");
    layout_row->addWidget(m_btn_zoom_col_out);

    left->addLayout(layout_row);

    // Image views
    m_image_widget = new ImageView();
    m_image_views.append(m_image_widget);
    for (int i = 1; i < 4; i++)
        m_image_views.append(new ImageView());

    m_display_container = new QWidget();
    m_display_layout = new QVBoxLayout(m_display_container);
    m_display_layout->setContentsMargins(0, 0, 0, 0);
    m_display_layout->setSpacing(2);
    m_display_layout->addWidget(m_image_views[0]);

    left->addWidget(m_display_container, 1);
    splitter->addWidget(left_panel);

    // ── Right panel ──
    auto* right = new QWidget();
    right->setFixedWidth(350);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setSpacing(6);
    right_layout->setContentsMargins(0, 0, 0, 0);

    build_camera_selector_group(right_layout);

    m_tabs_right = new QTabWidget();
    m_tabs_right->addTab(create_tab_page(&MainWindow::build_trigger_tab), "触发");
    m_tabs_right->addTab(create_tab_page(&MainWindow::build_image_tab), "图像");
    m_tabs_right->addTab(create_tab_page(&MainWindow::build_system_tab), "系统");
    right_layout->addWidget(m_tabs_right);

    splitter->addWidget(right);
    splitter->setSizes({1000, 360});

    // Status bar
    m_status_widget = new StatusBarWidget();
    statusBar()->addPermanentWidget(m_status_widget);

    apply_display_mode();
}

QWidget* MainWindow::create_tab_page(void (MainWindow::*builder)(QVBoxLayout*)) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* container = new QWidget(); 
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    (this->*builder)(layout);
    layout->addStretch();

    scroll->setWidget(container);
    return scroll;
}

// ══════════════════════════════════════════════════
// Tab builders
// ══════════════════════════════════════════════════

void MainWindow::build_trigger_tab(QVBoxLayout* parent) {
    build_trigger_mode_group(parent);
    build_sample_rate_group(parent);
    build_3d_interval_group(parent);
    build_work_mode_group(parent);
}

void MainWindow::build_image_tab(QVBoxLayout* parent) {
    build_cam_group(parent);
    build_exposure_delay_group(parent);
    build_measure_range_group(parent);
    //build_image_enhance_group(parent);
    //build_image_display_group(parent);
    //build_image_roi_group(parent);
}

void MainWindow::build_system_tab(QVBoxLayout* parent) {
    build_net_group(parent);
    build_device_ip_group(parent);
    build_firmware_upgrade_group(parent);
    build_system_info_group(parent);
    //build_system_sync_group(parent);
    build_status_group(parent);
    build_protocol_analyzer_group(parent);
}

// ══════════════════════════════════════════════════
// Camera selector
// ══════════════════════════════════════════════════

void MainWindow::build_camera_selector_group(QVBoxLayout* parent) {
    m_cam_selector_group = new QGroupBox("相机选择");
    auto* lay = new QHBoxLayout(m_cam_selector_group);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 20, 8, 8);

    m_camera_group = new QButtonGroup(this);
    m_camera_group->setExclusive(true);

    auto* radio_all = new QRadioButton("All");
    m_camera_group->addButton(radio_all, 0);
    lay->addWidget(radio_all);

    for (int i = 1; i <= 4; i++) {
        auto* radio = new QRadioButton(QString("Cam%1").arg(i));
        m_camera_group->addButton(radio, i);
        lay->addWidget(radio);
        if (i == 1) radio->setChecked(true);
    }
    parent->addWidget(m_cam_selector_group);
}

// ══════════════════════════════════════════════════
// Trigger mode group
// ══════════════════════════════════════════════════

void MainWindow::build_trigger_mode_group(QVBoxLayout* parent) {
    m_grp_trigger_mode = new QGroupBox("触发模式");
    auto* lay = new QVBoxLayout(m_grp_trigger_mode);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    auto* row_mode = new QHBoxLayout();
    row_mode->addWidget(new QLabel("触发模式:"));
    m_combo_trigger_mode = new QComboBox();
    m_combo_trigger_mode->addItems({"连续触发", "编码器触发", "外部触发"});
    m_combo_trigger_mode->setCurrentIndex(0);
    row_mode->addWidget(m_combo_trigger_mode, 1);
    lay->addLayout(row_mode);

    m_encoder_widget = new QWidget();
    auto* encoder_form = new QFormLayout(m_encoder_widget);
    encoder_form->setSpacing(4);
    encoder_form->setContentsMargins(0, 0, 0, 0);

    m_combo_encoder_type = new QComboBox();
    m_combo_encoder_type->addItems({"差分", "单端"});
    encoder_form->addRow("编码器类型:", m_combo_encoder_type);

    m_combo_encoder_mode = new QComboBox();
    m_combo_encoder_mode->addItems({"2相4递增", "2相2递增", "2相1递增"});
    encoder_form->addRow("输入模式:", m_combo_encoder_mode);

    m_spin_encoder_subdivision = new QSpinBox();
    m_spin_encoder_subdivision->setRange(1, 256);
    m_spin_encoder_subdivision->setValue(4);
    encoder_form->addRow("细化点数:", m_spin_encoder_subdivision);

    m_encoder_widget->setVisible(false);
    lay->addWidget(m_encoder_widget);

    parent->addWidget(m_grp_trigger_mode);
}

void MainWindow::build_sample_rate_group(QVBoxLayout* parent) {
    m_grp_sample_rate = new QGroupBox("采样频率");
    auto* lay = new QHBoxLayout(m_grp_sample_rate);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    lay->addWidget(new QLabel("最快采样频率:"));

    m_combo_sample_rate = new QComboBox();
    QStringList rates = {"10", "20", "30", "50", "60", "100", "200", "300",
                         "500", "800", "1200", "1500", "2000", "2500"};
    m_combo_sample_rate->addItems(rates);
    m_combo_sample_rate->setCurrentText("2500");
    lay->addWidget(m_combo_sample_rate, 1);

    auto* lbl_hz = new QLabel("Hz");
    lbl_hz->setStyleSheet("color: #00FFCC; font-weight: bold;");
    lay->addWidget(lbl_hz);

    parent->addWidget(m_grp_sample_rate);
}

void MainWindow::build_3d_interval_group(QVBoxLayout* parent) {
    m_grp_3d_interval = new QGroupBox("3D 显示间隔");
    auto* lay = new QHBoxLayout(m_grp_3d_interval);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    lay->addWidget(new QLabel("间隔:"));

    m_spin_3d_interval = new QDoubleSpinBox();
    m_spin_3d_interval->setRange(0.001, 100.0);
    m_spin_3d_interval->setValue(0.020);
    m_spin_3d_interval->setDecimals(3);
    m_spin_3d_interval->setSingleStep(0.001);
    m_spin_3d_interval->setSuffix(" mm");
    lay->addWidget(m_spin_3d_interval, 1);

    parent->addWidget(m_grp_3d_interval);
}

void MainWindow::build_work_mode_group(QVBoxLayout* parent) {
    m_grp_work_mode = new QGroupBox("工作模式");
    auto* lay = new QVBoxLayout(m_grp_work_mode);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    auto* form = new QFormLayout();
    form->setSpacing(4);
    m_spin_batch_count = new QSpinBox();
    m_spin_batch_count->setRange(1, 10000);
    m_spin_batch_count->setValue(100);
    form->addRow("批处理数量:", m_spin_batch_count);
    lay->addLayout(form);

    m_chk_external_trigger = new QCheckBox("外部信号触发开始");
    lay->addWidget(m_chk_external_trigger);

    auto* row_batch_btn = new QHBoxLayout();
    row_batch_btn->setSpacing(6);

    m_btn_batch_start = new QPushButton("开始批处理");
    m_btn_batch_start->setObjectName("batch_btn");
    row_batch_btn->addWidget(m_btn_batch_start);

    m_btn_batch_stop = new QPushButton("暂停批处理");
    m_btn_batch_stop->setObjectName("batch_btn");
    row_batch_btn->addWidget(m_btn_batch_stop);

    m_btn_batch_reset = new QPushButton("重置数据");
    m_btn_batch_reset->setObjectName("batch_btn");
    row_batch_btn->addWidget(m_btn_batch_reset);

    row_batch_btn->addStretch();
    lay->addLayout(row_batch_btn);

    parent->addWidget(m_grp_work_mode);
}

// ══════════════════════════════════════════════════
// Image tab groups
// ══════════════════════════════════════════════════

void MainWindow::build_cam_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("相机控制");
    auto* lay = new QVBoxLayout(g);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    // Exposure
    auto* exp_row = new QHBoxLayout();
    exp_row->addWidget(new QLabel("曝光 (us):"));
    m_exp_slider = new QSlider(Qt::Horizontal);
    m_exp_slider->setRange(1, 1000000); // in Python it's (1, 1000) but value is 10000 — fixed
    m_exp_slider->setValue(m_config.default_exposure);
    m_exp_spin = new QSpinBox();
    m_exp_spin->setRange(1, 1000000);
    m_exp_spin->setValue(m_config.default_exposure);
    m_exp_spin->setSuffix(" us");
    exp_row->addWidget(m_exp_slider, 1);
    exp_row->addWidget(m_exp_spin);
    lay->addLayout(exp_row);

    // Gain
    auto* gain_row = new QHBoxLayout();
    gain_row->addWidget(new QLabel("增益:"));
    m_gain_slider = new QSlider(Qt::Horizontal);
    m_gain_slider->setRange(0, 100);
    m_gain_slider->setValue(m_config.default_gain);
    m_gain_spin = new QSpinBox();
    m_gain_spin->setRange(0, 100);
    m_gain_spin->setValue(m_config.default_gain);
    gain_row->addWidget(m_gain_slider, 1);
    gain_row->addWidget(m_gain_spin);
    lay->addLayout(gain_row);

    // Buttons
    auto* btn_row = new QHBoxLayout();
    m_btn_apply = new QPushButton("下发配置");
    m_btn_read = new QPushButton("读取当前");
    btn_row->addWidget(m_btn_apply);
    btn_row->addWidget(m_btn_read);
    lay->addLayout(btn_row);

    parent->addWidget(g);
}

void MainWindow::build_exposure_delay_group(QVBoxLayout* parent) {
    m_exp_delay_group = new QGroupBox("曝光延迟配置");
    auto* lay = new QVBoxLayout(m_exp_delay_group);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    for (int i = 1; i <= 4; i++) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(QString("Cam%1 延迟 (us):").arg(i)));
        auto* spin = new QSpinBox();
        spin->setRange(0, 1000000);
        spin->setValue(0);
        spin->setSuffix(" us");
        row->addWidget(spin, 1);
        lay->addLayout(row);
        m_exp_delay_spins.append(spin);
    }

    m_btn_exp_delay_send = new QPushButton("下发延迟配置");
    lay->addWidget(m_btn_exp_delay_send);

    m_exp_delay_group->hide();
    parent->addWidget(m_exp_delay_group);
}

void MainWindow::build_measure_range_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("测量范围");
    auto* lay = new QGridLayout(g);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    QStringList options = {"1/8", "1/4", "1/2", "3/4", "1"};
    for (int i = 0; i < options.size(); i++) {
        auto* btn = new QRadioButton(options[i]);
        if (options[i] == "1") btn->setChecked(true);
        lay->addWidget(btn, i / 4, i % 4);
    }
    parent->addWidget(g);
}

//void MainWindow::build_image_enhance_group(QVBoxLayout* parent) {
//    auto* g = new QGroupBox("图像增强");
//    auto* lay = new QGridLayout(g);
//    lay->setSpacing(6);
//    lay->setContentsMargins(8, 20, 8, 8);
//
//    lay->addWidget(new QCheckBox("自动曝光"), 0, 0);
//    lay->addWidget(new QCheckBox("自动增益"), 0, 1);
//    lay->addWidget(new QCheckBox("自动白平衡"), 1, 0);
//    lay->addWidget(new QCheckBox("自动对比度"), 1, 1);
//
//    lay->addWidget(new QLabel("亮度:"), 2, 0);
//    auto* spin_brightness = new QSpinBox();
//    spin_brightness->setRange(0, 255);
//    spin_brightness->setValue(120);
//    lay->addWidget(spin_brightness, 2, 1);
//
//    lay->addWidget(new QLabel("Gamma:"), 3, 0);
//    auto* spin_gamma = new QSpinBox();
//    spin_gamma->setRange(1, 400);
//    spin_gamma->setValue(100);
//    lay->addWidget(spin_gamma, 3, 1);
//
//    parent->addWidget(g);
//}
//
//void MainWindow::build_image_display_group(QVBoxLayout* parent) {
//    auto* g = new QGroupBox("图像显示");
//    auto* lay = new QVBoxLayout(g);
//    lay->setSpacing(6);
//    lay->setContentsMargins(8, 20, 8, 8);
//
//    auto* row_mode = new QHBoxLayout();
//    row_mode->addWidget(new QRadioButton("灰度"));
//    row_mode->addWidget(new QRadioButton("伪彩"));
//    row_mode->addWidget(new QRadioButton("轮廓"));
//    row_mode->addStretch();
//    lay->addLayout(row_mode);
//
//    auto* grid = new QGridLayout();
//    grid->addWidget(new QLabel("最小值:"), 0, 0);
//    auto* spin_min = new QSpinBox();
//    spin_min->setRange(0, 255);
//    spin_min->setValue(0);
//    grid->addWidget(spin_min, 0, 1);
//
//    grid->addWidget(new QLabel("最大值:"), 1, 0);
//    auto* spin_max = new QSpinBox();
//    spin_max->setRange(0, 255);
//    spin_max->setValue(255);
//    grid->addWidget(spin_max, 1, 1);
//
//    grid->addWidget(new QLabel("平滑:"), 2, 0);
//    auto* spin_smooth = new QSpinBox();
//    spin_smooth->setRange(0, 20);
//    spin_smooth->setValue(3);
//    grid->addWidget(spin_smooth, 2, 1);
//    lay->addLayout(grid);
//
//    auto* row_flags = new QHBoxLayout();
//    row_flags->addWidget(new QCheckBox("叠加坐标"));
//    row_flags->addWidget(new QCheckBox("中心线"));
//    row_flags->addWidget(new QCheckBox("反相"));
//    row_flags->addStretch();
//    lay->addLayout(row_flags);
//
//    parent->addWidget(g);
//}
//
//void MainWindow::build_image_roi_group(QVBoxLayout* parent) {
//    auto* g = new QGroupBox("图像区域");
//    auto* lay = new QFormLayout(g);
//    lay->setSpacing(4);
//    lay->setContentsMargins(8, 20, 8, 8);
//
//    auto* combo_zoom = new QComboBox();
//    combo_zoom->addItems({"适应窗口", "1:1", "2x", "4x"});
//    lay->addRow("缩放:", combo_zoom);
//
//    auto* combo_palette = new QComboBox();
//    combo_palette->addItems({"默认", "Iron", "Jet", "灰阶"});
//    lay->addRow("色表:", combo_palette);
//
//    lay->addRow("ROI X:", new QSpinBox());
//    lay->addRow("ROI Y:", new QSpinBox());
//
//    auto* row_size = new QHBoxLayout();
//    auto* spin_w = new QSpinBox();
//    spin_w->setRange(0, 4096);
//    spin_w->setValue(1280);
//    auto* spin_h = new QSpinBox();
//    spin_h->setRange(0, 4096);
//    spin_h->setValue(1024);
//    row_size->addWidget(spin_w);
//    row_size->addWidget(new QLabel("×"));
//    row_size->addWidget(spin_h);
//    lay->addRow("ROI 尺寸:", row_size);
//
//    parent->addWidget(g);
//}

// ══════════════════════════════════════════════════
// System tab groups
// ══════════════════════════════════════════════════

void MainWindow::build_net_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("网络连接");
    auto* lay = new QFormLayout(g);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 20, 8, 8);

    auto* fpga_row = new QHBoxLayout();
    m_edit_fpga = new QLineEdit(m_config.target_host);
    fpga_row->addWidget(m_edit_fpga, 1);

    m_btn_refresh_fpga = new QPushButton(QString::fromUtf8("↻"));
    m_btn_refresh_fpga->setFixedSize(28, 28);
    m_btn_refresh_fpga->setToolTip("刷新");
    m_btn_refresh_fpga->setStyleSheet(
        "QPushButton { padding: 0px; font-size: 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: palette(highlight); }");
    fpga_row->addWidget(m_btn_refresh_fpga);
    lay->addRow("FPGA IP:", fpga_row);

    m_combo_host = new QComboBox();
    m_combo_host->setEditable(true);
    refresh_local_ips(m_combo_host);
    m_spin_fpga_port = new QSpinBox();
    m_spin_fpga_port->setRange(1024, 65535);
    m_spin_fpga_port->setValue(m_config.target_port);
    m_spin_local_port = new QSpinBox();
    m_spin_local_port->setRange(1024, 65535);
    m_spin_local_port->setValue(m_config.control_port);

    lay->addRow("本机 IP:", m_combo_host);
    lay->addRow("FPGA 端口:", m_spin_fpga_port);
    lay->addRow("本机端口:", m_spin_local_port);

    auto* btn_row = new QHBoxLayout();
    m_btn_connect = new QPushButton("连接");
    m_btn_disconnect = new QPushButton("断开");
    m_btn_disconnect->setEnabled(false);
    btn_row->addWidget(m_btn_connect);
    btn_row->addWidget(m_btn_disconnect);
    lay->addRow(btn_row);

    parent->addWidget(g);
}

void MainWindow::build_device_ip_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("设备IP设置");
    auto* lay = new QFormLayout(g);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 20, 8, 8);

    m_edit_device_ip = new QLineEdit("192.168.10.10");
    m_edit_device_mask = new QLineEdit("255.255.255.0");
    m_edit_device_gateway = new QLineEdit("192.168.10.1");
    m_edit_device_mac = new QLineEdit("00-11-22-33-44-55");
    m_combo_local_ip = new QComboBox();
    m_combo_local_ip->setEditable(true);
    refresh_local_ips(m_combo_local_ip);

    lay->addRow("设备IP:", m_edit_device_ip);
    lay->addRow("本机IP:", m_combo_local_ip);
    lay->addRow("子网掩码:", m_edit_device_mask);
    lay->addRow("默认网关:", m_edit_device_gateway);
    lay->addRow("设备MAC:", m_edit_device_mac);

    auto* row_btn = new QHBoxLayout();
    row_btn->addWidget(new QPushButton("读取"));
    row_btn->addWidget(new QPushButton("设置"));
    lay->addRow(row_btn);
    parent->addWidget(g);
}

void MainWindow::build_firmware_upgrade_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("固件升级");
    auto* lay = new QVBoxLayout(g);
    lay->setSpacing(6);
    lay->setContentsMargins(8, 20, 8, 8);

    auto* row_file = new QHBoxLayout();
    m_edit_firmware_path = new QLineEdit();
    m_edit_firmware_path->setPlaceholderText("请选择固件文件 (.bin)...");
    m_edit_firmware_path->setReadOnly(true);
    row_file->addWidget(m_edit_firmware_path, 1);
    m_btn_browse_firmware = new QPushButton("浏览");
    row_file->addWidget(m_btn_browse_firmware);
    lay->addLayout(row_file);

    m_progress_firmware = new QProgressBar();
    m_progress_firmware->setRange(0, 100);
    m_progress_firmware->setValue(0);
    lay->addWidget(m_progress_firmware);

    m_lbl_upgrade_status = new QLabel("升级状态显示");
    m_lbl_upgrade_status->setWordWrap(true);
    lay->addWidget(m_lbl_upgrade_status);

    auto* row_btn = new QHBoxLayout();
    m_btn_upgrade_start = new QPushButton("升级");
    m_btn_upgrade_start->setEnabled(false);
    row_btn->addWidget(m_btn_upgrade_start);
    lay->addLayout(row_btn);

    parent->addWidget(g);
}

void MainWindow::build_system_info_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("设备信息");
    auto* lay = new QFormLayout(g);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 20, 8, 8);

    lay->addRow("设备型号:", new QLabel("FPGA-CAM-01"));
    m_lbl_fw_version = new QLabel("---");
    m_lbl_fw_version->setObjectName("val_label");
    lay->addRow("固件版本:", m_lbl_fw_version);
    lay->addRow("协议版本:", new QLabel("UDP/AA55"));
    lay->addRow("序列号:", new QLabel("SN-20260626"));
    lay->addRow("工作状态:", new QLabel("待连接"));

    m_btn_sdk_test = new QPushButton("运行 SDK 功能测试");
    m_btn_sdk_test->setObjectName("sdk_test_btn");
    lay->addRow(m_btn_sdk_test);

    parent->addWidget(g);
}

//void MainWindow::build_system_sync_group(QVBoxLayout* parent) {
//    auto* g = new QGroupBox("系统配置");
//    auto* lay = new QGridLayout(g);
//    lay->setSpacing(6);
//    lay->setContentsMargins(8, 20, 8, 8);
//
//    lay->addWidget(new QCheckBox("启用 NTP"), 0, 0);
//    lay->addWidget(new QCheckBox("自动重连"), 0, 1);
//    lay->addWidget(new QCheckBox("日志落盘"), 1, 0);
//    lay->addWidget(new QCheckBox("开机自检"), 1, 1);
//
//    lay->addWidget(new QLabel("缓存帧数:"), 2, 0);
//    auto* spin_cache = new QSpinBox();
//    spin_cache->setRange(1, 200);
//    spin_cache->setValue(16);
//    lay->addWidget(spin_cache, 2, 1);
//
//    lay->addWidget(new QLabel("保存路径:"), 3, 0);
//    lay->addWidget(new QLineEdit("captured_images/"), 3, 1);
//
//    parent->addWidget(g);
//}

void MainWindow::build_status_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("系统状态");
    auto* lay = new QFormLayout(g);
    lay->setSpacing(4);
    lay->setContentsMargins(8, 20, 8, 8);

    m_lbl_fps_val = new QLabel("0");
    m_lbl_fps_val->setObjectName("val_label");
    m_lbl_frame_val = new QLabel("0");
    m_lbl_frame_val->setObjectName("val_label");
    m_lbl_dropped_val = new QLabel("0");
    m_lbl_dropped_val->setObjectName("val_label");
    m_lbl_pkt_val = new QLabel("-");
    m_lbl_pkt_val->setObjectName("val_label");
    m_lbl_res_val = new QLabel("1280×1024");
    m_lbl_res_val->setObjectName("val_label");

    lay->addRow("FPS:", m_lbl_fps_val);
    lay->addRow("总帧:", m_lbl_frame_val);
    lay->addRow("丢包:", m_lbl_dropped_val);
    lay->addRow("PKT_ID:", m_lbl_pkt_val);
    lay->addRow("分辨率:", m_lbl_res_val);

    parent->addWidget(g);
}

void MainWindow::build_protocol_analyzer_group(QVBoxLayout* parent) {
    auto* g = new QGroupBox("协议分析器");
    auto* lay = new QVBoxLayout(g);
    lay->setContentsMargins(8, 20, 8, 8);
    lay->setSpacing(6);

    auto* desc = new QLabel("粘贴十六进制帧数据，逐层解析传输层和内部协议帧");
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #888; font-size: 11px;");
    lay->addWidget(desc);

    m_btn_protocol_analyzer = new QPushButton("打开协议分析器");
    lay->addWidget(m_btn_protocol_analyzer);

    parent->addWidget(g);
}

// ══════════════════════════════════════════════════
// Signal connections
// ══════════════════════════════════════════════════

void MainWindow::connect_signals() {
    // Protocol signals
    if (m_protocol) {
        connect(m_protocol, &ProtocolHandler::connected, this, &MainWindow::on_connected);
        connect(m_protocol, &ProtocolHandler::disconnected, this, &MainWindow::on_disconnected);
        connect(m_protocol, &ProtocolHandler::control_response, this, &MainWindow::on_control_response);
        connect(m_protocol, &ProtocolHandler::data_frame_info, this, &MainWindow::on_frame_info);
        connect(m_protocol, &ProtocolHandler::data_resolution, this, &MainWindow::on_resolution_changed);
        connect(m_protocol, &ProtocolHandler::status_message, this, &MainWindow::on_status_message);
        connect(m_protocol, &ProtocolHandler::error_occurred, this, &MainWindow::on_error_occurred);
    }

    // Layout switching
    if (m_layout_group) {
        connect(m_layout_group, &QButtonGroup::idToggled, this, &MainWindow::on_layout_changed);
    }

    // Mode buttons
    if (m_btn_show_image) connect(m_btn_show_image, &QPushButton::toggled, this, &MainWindow::on_image_toggled);
    if (m_btn_show_contour) connect(m_btn_show_contour, &QPushButton::toggled, this, &MainWindow::on_contour_toggled);
    if (m_btn_show_3d) connect(m_btn_show_3d, &QPushButton::toggled, this, &MainWindow::on_3d_toggled);
    if (m_btn_show_centerline) connect(m_btn_show_centerline, &QPushButton::toggled, this, &MainWindow::on_centerline_toggled);
    if (m_btn_save) connect(m_btn_save, &QPushButton::clicked, this, &MainWindow::on_save_image);

    // Measure buttons
    if (m_btn_measure_v) connect(m_btn_measure_v, &QPushButton::toggled, this, &MainWindow::on_measure_toggled_v);
    if (m_btn_measure_h) connect(m_btn_measure_h, &QPushButton::toggled, this, &MainWindow::on_measure_toggled_h);

    // Zoom
    if (m_btn_zoom_row_in) connect(m_btn_zoom_row_in, &QPushButton::clicked, this, &MainWindow::on_zoom_row_in);
    if (m_btn_zoom_row_out) connect(m_btn_zoom_row_out, &QPushButton::clicked, this, &MainWindow::on_zoom_row_out);
    if (m_btn_zoom_fit) connect(m_btn_zoom_fit, &QPushButton::clicked, this, &MainWindow::on_zoom_fit);
    if (m_btn_zoom_col_in) connect(m_btn_zoom_col_in, &QPushButton::clicked, this, &MainWindow::on_zoom_col_in);
    if (m_btn_zoom_col_out) connect(m_btn_zoom_col_out, &QPushButton::clicked, this, &MainWindow::on_zoom_col_out);

    // Camera selector
    if (m_camera_group) {
        connect(m_camera_group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
            if (!checked) return;
            m_selected_camera = id;
            if (m_exp_delay_group) m_exp_delay_group->setVisible(m_selected_camera == 0);
        });
    }

    // Tab change
    if (m_tabs_right) connect(m_tabs_right, &QTabWidget::currentChanged, this, &MainWindow::on_tab_changed);

    // Trigger mode
    if (m_combo_trigger_mode) connect(m_combo_trigger_mode, &QComboBox::currentIndexChanged, this, &MainWindow::on_trigger_mode_changed);

    // Sample rate
    if (m_combo_sample_rate) {
        connect(m_combo_sample_rate, &QComboBox::currentTextChanged, this, [this](const QString& v) {
            if (m_status_widget) m_status_widget->set_sample_rate(v + " Hz");
            on_sample_rate_changed(v);
        });
    }

    // Encoder
    if (m_combo_encoder_type) connect(m_combo_encoder_type, &QComboBox::currentIndexChanged, this, &MainWindow::on_encoder_type_changed);
    if (m_combo_encoder_mode) connect(m_combo_encoder_mode, &QComboBox::currentIndexChanged, this, &MainWindow::on_encoder_mode_changed);
    if (m_spin_encoder_subdivision) connect(m_spin_encoder_subdivision, &QSpinBox::valueChanged, this, &MainWindow::on_encoder_interval_changed);

    // External trigger
    if (m_chk_external_trigger) connect(m_chk_external_trigger, &QCheckBox::toggled, this, &MainWindow::on_external_trigger_toggled);

    // Camera control
    if (m_exp_slider && m_exp_spin) {
        connect(m_exp_slider, &QSlider::valueChanged, m_exp_spin, &QSpinBox::setValue);
        connect(m_exp_spin, &QSpinBox::valueChanged, m_exp_slider, &QSlider::setValue);
        connect(m_exp_slider, &QSlider::sliderReleased, this, &MainWindow::on_exp_changed);
        connect(m_exp_spin, &QSpinBox::editingFinished, this, &MainWindow::on_exp_changed);
    }
    if (m_gain_slider && m_gain_spin) {
        connect(m_gain_slider, &QSlider::valueChanged, m_gain_spin, &QSpinBox::setValue);
        connect(m_gain_spin, &QSpinBox::valueChanged, m_gain_slider, &QSlider::setValue);
        connect(m_gain_slider, &QSlider::sliderReleased, this, &MainWindow::on_gain_changed);
        connect(m_gain_spin, &QSpinBox::editingFinished, this, &MainWindow::on_gain_changed);
    }

    if (m_btn_apply) connect(m_btn_apply, &QPushButton::clicked, this, &MainWindow::on_apply_params);
    if (m_btn_read) connect(m_btn_read, &QPushButton::clicked, this, &MainWindow::on_read_params);

    // Exposure delay
    if (m_btn_exp_delay_send) connect(m_btn_exp_delay_send, &QPushButton::clicked, this, &MainWindow::on_send_exp_delay);

    // Network
    if (m_btn_connect) connect(m_btn_connect, &QPushButton::clicked, this, &MainWindow::on_net_connect);
    if (m_btn_disconnect) connect(m_btn_disconnect, &QPushButton::clicked, this, &MainWindow::on_net_disconnect);
    if (m_btn_refresh_fpga) {
        connect(m_btn_refresh_fpga, &QPushButton::clicked, this, [this]() {
            refresh_local_ips(m_combo_host);
        });
    }

    // Batch
    if (m_spin_batch_count) connect(m_spin_batch_count, &QSpinBox::valueChanged, this, &MainWindow::on_batch_count_changed);
    if (m_btn_batch_start) connect(m_btn_batch_start, &QPushButton::clicked, this, &MainWindow::on_batch_start);
    if (m_btn_batch_stop) connect(m_btn_batch_stop, &QPushButton::clicked, this, &MainWindow::on_batch_stop);
    if (m_btn_batch_reset) connect(m_btn_batch_reset, &QPushButton::clicked, this, &MainWindow::on_batch_reset);

    // Firmware
    if (m_btn_browse_firmware) connect(m_btn_browse_firmware, &QPushButton::clicked, this, &MainWindow::on_browse_firmware);
    if (m_btn_upgrade_start) connect(m_btn_upgrade_start, &QPushButton::clicked, this, &MainWindow::on_upgrade_start);

    // Protocol analyzer
    if (m_btn_protocol_analyzer) connect(m_btn_protocol_analyzer, &QPushButton::clicked, this, &MainWindow::on_open_protocol_analyzer);

    // SDK 功能测试
    if (m_btn_sdk_test) connect(m_btn_sdk_test, &QPushButton::clicked, this, &MainWindow::on_sdk_test);

    // Batch complete from views
    for (auto* view : m_image_views) {
        if (view) {
            connect(view, &ImageView::batch_complete, this, &MainWindow::on_batch_complete);
        }
    }

    // Initial status bar
    if (m_status_widget && m_combo_sample_rate) {
        m_status_widget->set_sample_rate(m_combo_sample_rate->currentText() + " Hz");
    }
}

// ══════════════════════════════════════════════════
// Frame polling (QTimer, 10ms)
// ══════════════════════════════════════════════════

void MainWindow::poll_frame() {
    bool any_new = false;
    for (int sid = 1; sid <= 4; sid++) {
        cv::Mat img = m_protocol->get_latest_frame(sid);
        if (!img.empty()) {
            m_camera_frames[sid].set_image(img);
            if (!any_new && !m_image_logged) {
                log(QString("← 收到图像 Sensor%1: %2×%3")
                    .arg(sid).arg(img.cols).arg(img.rows));
                m_image_logged = true;
            }
            any_new = true;
        }
    }
    if (!any_new) return;

    // Distribute to views
    if (m_current_layout == 1) {
        if (m_selected_camera == 0) {
            for (int sid = 1; sid <= 4; sid++) {
                auto& frame = m_camera_frames[sid];
                if (frame.has_image) {
                    m_image_views[0]->set_camera_frame(frame);
                    m_image_views[0]->set_camera_label(QString("Cam%1").arg(sid));
                    break;
                }
            }
        } else {
            m_image_views[0]->set_camera_frame(m_camera_frames[m_selected_camera]);
            m_image_views[0]->set_camera_label(QString("Cam%1").arg(m_selected_camera));
        }
    } else if (m_current_layout == 2) {
        for (int i = 0; i < 2; i++) {
            m_image_views[i]->set_camera_frame(m_camera_frames[i + 1]);
            m_image_views[i]->set_camera_label(QString("Cam%1").arg(i + 1));
        }
    } else {
        for (int i = 0; i < 4; i++) {
            m_image_views[i]->set_camera_frame(m_camera_frames[i + 1]);
            m_image_views[i]->set_camera_label(QString("Cam%1").arg(i + 1));
        }
    }

    int pkt_id = m_protocol->last_pkt_id();
    m_lbl_pkt_val->setText(pkt_id >= 0 ? QString::number(pkt_id) : "-");
}

QVector<int> MainWindow::visible_camera_indices() const {
    if (m_current_layout == 1)
        return (m_selected_camera == 0) ? QVector<int>{0} : QVector<int>{m_selected_camera - 1};
    else if (m_current_layout == 2)
        return {0, 1};
    else
        return {0, 1, 2, 3};
}

// ══════════════════════════════════════════════════
// Display mode handlers
// ══════════════════════════════════════════════════

void MainWindow::on_image_toggled(bool checked) {
    if (checked) {
        m_btn_show_contour->setChecked(false);
        m_btn_show_3d->setChecked(false);
    }
    apply_display_mode();
}

void MainWindow::on_contour_toggled(bool checked) {
    if (checked) {
        m_btn_show_image->setChecked(false);
        m_btn_show_3d->setChecked(false);
        m_btn_show_centerline->setChecked(false);
    }
    apply_display_mode();
}

void MainWindow::on_centerline_toggled(bool checked) {
    if (checked) {
        m_btn_show_contour->setChecked(false);
        m_btn_show_3d->setChecked(false);
        if (!m_btn_show_image->isChecked())
            m_btn_show_image->setChecked(true);
    }
    apply_display_mode();
}

void MainWindow::on_3d_toggled(bool checked) {
    if (checked) {
        m_btn_show_image->setChecked(false);
        m_btn_show_contour->setChecked(false);
        m_btn_show_centerline->setChecked(false);
    }
    apply_display_mode();
}

void MainWindow::on_measure_toggled_v(bool checked) {
    for (auto* view : m_image_views) {
        view->set_measure_line_visible("vertical", 0, checked);
        view->set_measure_line_visible("vertical", 1, checked);
    }
}

void MainWindow::on_measure_toggled_h(bool checked) {
    for (auto* view : m_image_views) {
        view->set_measure_line_visible("horizontal", 0, checked);
        view->set_measure_line_visible("horizontal", 1, checked);
    }
}

void MainWindow::apply_display_mode() {
    bool show_image = m_btn_show_image->isChecked();
    bool show_contour = m_btn_show_contour->isChecked();
    bool show_centerline = m_btn_show_centerline->isChecked();
    bool show_3d = m_btn_show_3d->isChecked();

    for (auto* view : m_image_views) {
        view->set_show_image(show_image);
        view->set_show_contour(show_contour);
        view->set_show_centerline(show_centerline);
        view->set_show_3d(show_3d);
    }

    m_btn_show_centerline->setVisible(show_image);
    m_btn_measure_v->setVisible(show_contour);
    m_btn_measure_h->setVisible(show_contour);

    if (!show_contour) {
        m_btn_measure_v->setChecked(false);
        m_btn_measure_h->setChecked(false);
        for (auto* view : m_image_views)
            view->set_measure_mode({});
    }
}

// ══════════════════════════════════════════════════
// Layout / Zoom
// ══════════════════════════════════════════════════

void MainWindow::on_layout_changed(int id, bool checked) {
    if (!checked) return;
    set_layout(id);
}

void MainWindow::set_layout(int count) {
    m_current_layout = count;

    for (auto* view : m_image_views)
        view->setParent(nullptr);

    while (m_display_layout->count()) {
        auto* item = m_display_layout->takeAt(0);
        if (item->widget())
            item->widget()->setParent(nullptr);
    }

    if (count == 1) {
        m_display_layout->addWidget(m_image_views[0]);
    } else if (count == 2) {
        auto* splitter = new QSplitter(Qt::Horizontal);
        splitter->setHandleWidth(3);
        splitter->setStyleSheet("QSplitter::handle { background-color: #3A3A3A; }");
        splitter->setChildrenCollapsible(false);
        splitter->addWidget(m_image_views[0]);
        splitter->addWidget(m_image_views[1]);
        m_display_layout->addWidget(splitter);
    } else {
        auto* h_top = new QSplitter(Qt::Horizontal);
        h_top->setHandleWidth(3);
        h_top->setStyleSheet("QSplitter::handle { background-color: #3A3A3A; }");
        h_top->setChildrenCollapsible(false);
        h_top->addWidget(m_image_views[0]);
        h_top->addWidget(m_image_views[1]);

        auto* h_bot = new QSplitter(Qt::Horizontal);
        h_bot->setHandleWidth(3);
        h_bot->setStyleSheet("QSplitter::handle { background-color: #3A3A3A; }");
        h_bot->setChildrenCollapsible(false);
        h_bot->addWidget(m_image_views[2]);
        h_bot->addWidget(m_image_views[3]);

        auto* v_splitter = new QSplitter(Qt::Vertical);
        v_splitter->setHandleWidth(3);
        v_splitter->setStyleSheet("QSplitter::handle { background-color: #3A3A3A; }");
        v_splitter->setChildrenCollapsible(false);
        v_splitter->addWidget(h_top);
        v_splitter->addWidget(h_bot);
        m_display_layout->addWidget(v_splitter);
    }
}

void MainWindow::on_zoom_row_in() { active_view()->zoom_row_in(); }
void MainWindow::on_zoom_row_out() { active_view()->zoom_row_out(); }
void MainWindow::on_zoom_col_in() { active_view()->zoom_col_in(); }
void MainWindow::on_zoom_col_out() { active_view()->zoom_col_out(); }
void MainWindow::on_zoom_fit() { active_view()->reset_zoom(); }

// ══════════════════════════════════════════════════
// Network handlers
// ══════════════════════════════════════════════════

void MainWindow::on_net_connect() {
    QString fpga_ip = m_edit_fpga->text();
    QHostAddress addr(fpga_ip);
    if (addr.isNull() || addr.isLoopback()) {
        log_error(QString("FPGA IP 无效: %1").arg(fpga_ip));
        return;
    }
    int port_fpga = m_spin_fpga_port->value();
    int port_local = m_spin_local_port->value();

    m_protocol->set_network_params(fpga_ip, port_fpga, port_local);
    m_protocol->start_all();

    m_btn_connect->setEnabled(false);
    m_btn_disconnect->setEnabled(true);
    log(QString("连接 FPGA: %1:%2 (本机端口:%3)").arg(fpga_ip).arg(port_fpga).arg(port_local));
}

void MainWindow::on_net_disconnect() {
    m_protocol->stop_all();
    m_btn_connect->setEnabled(true);
    m_btn_disconnect->setEnabled(false);
    m_image_logged = false;
    reset_upgrade_state();
    for (auto* view : m_image_views)
        view->clear_all();
    for (int sid = 1; sid <= 4; sid++)
        m_camera_frames[sid].clear();
    m_lbl_fw_version->setText("---");
    log("已断开连接");
}

void MainWindow::on_connected() {
    if (!m_status_widget) return;
    m_status_widget->set_ip(QString("%1:%2").arg(m_edit_fpga->text()).arg(m_spin_fpga_port->value()));
    log("FPGA 已连接");

    // Read FW version after connect
    if (!m_protocol) return;
    m_protocol->read_fpga_version(
        [this](const ControlFrame& r) {
            // Check if this object still exists before accessing members
            if (!m_lbl_fw_version) return;
            QString ver = QString::fromUtf8(r.data).trimmed();
            if (ver.isEmpty()) ver = "(empty)";
            m_lbl_fw_version->setText(ver);
            log(QString("FPGA 固件版本: %1").arg(ver));
        },
        [this]() { 
            if (!this) return;  // Redundant but safe
            log_error("读取 FPGA 固件版本超时"); 
        });
}

void MainWindow::on_disconnected() {
    m_status_widget->set_ip("---");
    log("FPGA 已断开");
}

void MainWindow::on_control_response(const ControlFrame& frame) {
    if (frame.msg_type == MSG_TYPE_RESPONSE) {
        log(QString("收到被动应答: cmd=0x%1").arg(frame.cmd_code, 4, 16, QChar('0')));
    }
}

void MainWindow::on_frame_info(int fps, int total, int dropped) {
    m_total_frames = total;
    m_dropped_packets = dropped;
    m_lbl_fps_val->setText(QString::number(fps));
    m_lbl_frame_val->setText(QString::number(total));
    m_lbl_dropped_val->setText(QString::number(dropped));
}

void MainWindow::on_resolution_changed(int sensor_id, int w, int h) {
    m_lbl_res_val->setText(QString("Cam%1: %2×%3").arg(sensor_id).arg(w).arg(h));
}

void MainWindow::on_status_message(const QString& msg) { log(msg); }
void MainWindow::on_error_occurred(const QString& msg) { log_error(msg); }

// ══════════════════════════════════════════════════
// Camera control handlers
// ══════════════════════════════════════════════════

void MainWindow::on_exp_changed() {
    int exp = m_exp_spin->value();
    for (int sid : target_sensor_ids()) {
        m_protocol->set_exposure(exp, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("曝光设置应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
    log(QString("下发曝光 [%1]: %2 us").arg(camera_label()).arg(exp));
}

void MainWindow::on_gain_changed() {
    int gain = m_gain_spin->value();
    for (int sid : target_sensor_ids()) {
        m_protocol->set_gain(gain, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("增益设置应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
    log(QString("下发增益 [%1]: %2").arg(camera_label()).arg(gain));
}

void MainWindow::on_apply_params() {
    int exp = m_exp_spin->value();
    int gain = m_gain_spin->value();
    for (int sid : target_sensor_ids()) {
        m_protocol->set_exposure(exp, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("曝光设置应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
        m_protocol->set_gain(gain, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("增益设置应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
    log(QString("下发配置 [%1]: 曝光=%2us  增益=%3").arg(camera_label()).arg(exp).arg(gain));
}

void MainWindow::on_read_params() {
    log(QString("正在读取相机参数 [%1]...").arg(camera_label()));
    int sid = target_sensor_ids()[0];
    m_protocol->read_exposure(sid,
        [this](const ControlFrame& r) { on_read_exp_done(r); });
    m_protocol->read_gain(sid,
        [this](const ControlFrame& r) { on_read_gain_done(r); });
}

void MainWindow::on_read_exp_done(const ControlFrame& frame) {
    if (frame.msg_type != MSG_TYPE_RESPONSE) {
        log(QString("曝光应答类型异常: %1").arg(frame.msg_type));
        return;
    }
    if (frame.data.size() >= 4) {
        uint32_t val = (static_cast<uint8_t>(frame.data[0]) << 24) |
                       (static_cast<uint8_t>(frame.data[1]) << 16) |
                       (static_cast<uint8_t>(frame.data[2]) << 8) |
                       static_cast<uint8_t>(frame.data[3]);
        m_exp_slider->setValue(static_cast<int>(val));
        log(QString("读取到曝光: %1 us").arg(val));
    }
}

void MainWindow::on_read_gain_done(const ControlFrame& frame) {
    if (frame.msg_type != MSG_TYPE_RESPONSE) return;
    if (frame.data.size() >= 4) {
        uint32_t val = (static_cast<uint8_t>(frame.data[0]) << 24) |
                       (static_cast<uint8_t>(frame.data[1]) << 16) |
                       (static_cast<uint8_t>(frame.data[2]) << 8) |
                       static_cast<uint8_t>(frame.data[3]);
        m_gain_slider->setValue(static_cast<int>(val));
        log(QString("读取到增益: %1").arg(val));
    }
}

void MainWindow::on_send_exp_delay() {
    QString msg = "下发曝光延迟:";
    for (int i = 0; i < 4; i++) {
        int delay = m_exp_delay_spins[i]->value();
        msg += QString(" Cam%1=%2us").arg(i + 1).arg(delay);
        m_protocol->set_exposure_delay(delay, i + 1,
            [this, sid = i + 1](const ControlFrame& r) {
                log(QString("曝光延迟应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
    log(msg);
}

// ══════════════════════════════════════════════════
// Trigger / Encoder handlers
// ══════════════════════════════════════════════════

void MainWindow::on_trigger_mode_changed(int index) {
    m_encoder_widget->setVisible(index == 1);
    static const int trigger_values[] = {1, 2, 3};
    int mode = trigger_values[index];
    for (int sid : target_sensor_ids()) {
        m_protocol->set_trigger_mode(mode, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("触发模式应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_sample_rate_changed(const QString& rate_str) {
    int rate = rate_str.toInt();
    for (int sid : target_sensor_ids()) {
        m_protocol->set_sample_rate(rate, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("采样频率应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_encoder_type_changed(int index) {
    int enc_type = (index == 0) ? 1 : 2;
    for (int sid : target_sensor_ids()) {
        m_protocol->set_encoder_type(enc_type, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("编码器类型应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_encoder_mode_changed(int index) {
    static const int encoder_modes[] = {1, 2, 3};
    int mode = encoder_modes[index];
    for (int sid : target_sensor_ids()) {
        m_protocol->set_encoder_mode(mode, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("编码器输入模式应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_encoder_interval_changed(int value) {
    for (int sid : target_sensor_ids()) {
        m_protocol->set_encoder_interval(value, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("编码器触发间隔应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_external_trigger_toggled(bool checked) {
    for (int sid : target_sensor_ids()) {
        m_protocol->set_external_trigger(checked, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("外部触发应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

// ══════════════════════════════════════════════════
// Batch handlers
// ══════════════════════════════════════════════════

void MainWindow::on_batch_start() {
    m_btn_show_image->setChecked(false);
    m_btn_show_contour->setChecked(false);
    m_btn_show_centerline->setChecked(false);
    m_btn_show_3d->setChecked(true);
    apply_display_mode();

    for (auto* view : m_image_views)
        view->start_batch();
    log("3D 批处理开始");
    set_batch_locked(true);

    for (int sid : target_sensor_ids()) {
        m_protocol->set_batch_control(true, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("批处理启动应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_batch_count_changed(int count) {
    for (auto* view : m_image_views)
        view->set_batch_total_count(count);
    for (int sid : target_sensor_ids()) {
        m_protocol->set_batch_length(count, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("批处理长度应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_batch_stop() {
    for (auto* view : m_image_views)
        view->stop_batch();
    log("3D 批处理停止");
    set_batch_locked(false);
    for (int sid : target_sensor_ids()) {
        m_protocol->set_batch_control(false, sid,
            [this, sid](const ControlFrame& r) {
                log(QString("批处理停止应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_batch_reset() {
    for (auto* view : m_image_views)
        view->reset_batch();
    log("3D 数据已重置");
    set_batch_locked(false);
    for (int sid : target_sensor_ids()) {
        m_protocol->reset_buffer(sid,
            [this, sid](const ControlFrame& r) {
                log(QString("重置缓存应答 (Cam%1): data=%2").arg(sid).arg(QString(r.data.toHex())));
            });
    }
}

void MainWindow::on_batch_complete() {
    log("3D 批处理完成（已达设定数量）");
    set_batch_locked(false);
}

void MainWindow::set_batch_locked(bool locked) {
    for (auto* btn : {m_btn_show_image, m_btn_show_contour, m_btn_show_centerline, m_btn_show_3d}) {
        btn->setEnabled(!locked);
        if (locked) {
            auto* eff = new QGraphicsOpacityEffect(btn);
            eff->setOpacity(0.4);
            btn->setGraphicsEffect(eff);
        } else {
            btn->setGraphicsEffect(nullptr);
        }
    }

    m_cam_selector_group->setEnabled(!locked);
    if (locked) {
        auto* eff = new QGraphicsOpacityEffect(m_cam_selector_group);
        eff->setOpacity(0.4);
        m_cam_selector_group->setGraphicsEffect(eff);
    } else {
        m_cam_selector_group->setGraphicsEffect(nullptr);
    }

    m_tabs_right->setTabEnabled(1, !locked);
    m_tabs_right->setTabEnabled(2, !locked);
    m_tabs_right->tabBar()->setEnabled(!locked);

    for (auto* grp : {m_grp_trigger_mode, m_grp_sample_rate, m_grp_3d_interval}) {
        grp->setEnabled(!locked);
        if (locked) {
            auto* eff = new QGraphicsOpacityEffect(grp);
            eff->setOpacity(0.4);
            grp->setGraphicsEffect(eff);
        } else {
            grp->setGraphicsEffect(nullptr);
        }
    }

    m_spin_batch_count->setEnabled(!locked);
    m_chk_external_trigger->setEnabled(!locked);
    m_btn_batch_start->setEnabled(!locked);
    m_btn_batch_reset->setEnabled(!locked);
}

// ══════════════════════════════════════════════════
// Firmware upgrade handlers
// ══════════════════════════════════════════════════

void MainWindow::on_browse_firmware() {
    QString file_path = QFileDialog::getOpenFileName(this, "选择固件文件", "",
        "固件文件 (*.bin);;所有文件 (*.*)");
    if (!file_path.isEmpty()) {
        m_firmware_path = file_path;
        m_edit_firmware_path->setText(file_path);
        m_btn_upgrade_start->setEnabled(true);
        log(QString("已选择固件文件: %1").arg(file_path));
    }
}

void MainWindow::on_upgrade_start() {
    if (m_firmware_path.isEmpty()) { log_error("请先选择固件文件"); return; }
    if (m_upgrade_in_progress) { log_error("升级正在进行中，请等待完成"); return; }

    try {
        m_firmware_chunks = split_firmware_chunks(m_firmware_path);
        m_firmware_total_chunks = m_firmware_chunks.size();
        m_firmware_chunk_index = 0;
    } catch (const std::exception& e) {
        log_error(QString("读取固件文件失败: %1").arg(e.what()));
        return;
    }

    m_upgrade_in_progress = true;
    set_upgrade_ui_enabled(false);
    m_progress_firmware->setValue(0);
    m_lbl_upgrade_status->setText("正在发送远程更新请求...");

    log(QString("=== 开始固件升级 ===\n  文件: %1\n  总块数: %2\n  块大小: %3 B")
        .arg(m_firmware_path).arg(m_firmware_total_chunks).arg(FIRMWARE_CHUNK_SIZE));

    m_protocol->send_remote_update_request(m_firmware_path,
        [this](const ControlFrame& r) { on_upgrade_request_done(r); },
        [this]() { on_upgrade_error(); });
}

void MainWindow::on_upgrade_request_done(const ControlFrame& frame) {
    if (frame.msg_type != MSG_TYPE_RESPONSE) { on_upgrade_error(); return; }
    auto data = frame.data;
    log(QString("← 远程更新请求应答: data=%1").arg(QString(data.toHex())));

    if (data.size() >= 1 && static_cast<uint8_t>(data[0]) == 0x00) {
        log("远程更新请求已确认，发送控制帧...");
        m_lbl_upgrade_status->setText("正在发送远程更新控制帧...");
        m_protocol->send_remote_update_control(true,
            [this](const ControlFrame& r) { on_upgrade_control_done(r); },
            [this]() { on_upgrade_error(); });
    } else {
        log_error(QString("远程更新请求被拒绝: %1").arg(QString(data.toHex())));
        reset_upgrade_state();
    }
}

void MainWindow::on_upgrade_control_done(const ControlFrame& frame) {
    if (frame.msg_type != MSG_TYPE_RESPONSE) { on_upgrade_error(); return; }
    auto data = frame.data;
    log(QString("← 远程更新控制应答: data=%1").arg(QString(data.toHex())));

    if (data.size() >= 1 && static_cast<uint8_t>(data[0]) == 0x00) {
        log("远程更新控制已确认，开始发送数据帧...");
        m_lbl_upgrade_status->setText("正在发送固件数据...");
        m_progress_firmware->setValue(0);
        send_next_chunk();
    } else {
        log_error(QString("远程更新控制被拒绝: %1").arg(QString(data.toHex())));
        reset_upgrade_state();
    }
}

void MainWindow::send_next_chunk() {
    if (m_firmware_chunk_index >= m_firmware_total_chunks) {
        log("所有数据块已发送，查询升级结果...");
        m_lbl_upgrade_status->setText("正在查询升级结果...");
        m_protocol->send_remote_update_result(
            [this](const ControlFrame& r) { on_upgrade_result(r); },
            [this]() { on_upgrade_error(); });
        return;
    }

    const auto& chunk = m_firmware_chunks[m_firmware_chunk_index];
    m_protocol->send_remote_update_data(m_firmware_chunk_index, chunk,
        [this](const ControlFrame& r) { on_chunk_done(r); },
        [this]() { on_chunk_error(); });
}

void MainWindow::on_chunk_done(const ControlFrame& /*frame*/) {
    m_firmware_chunk_index++;
    int progress = static_cast<int>(m_firmware_chunk_index * 100.0 / m_firmware_total_chunks);
    m_progress_firmware->setValue(std::min(progress, 99));
    m_lbl_upgrade_status->setText(QString("正在发送固件数据... (%1/%2)")
        .arg(m_firmware_chunk_index).arg(m_firmware_total_chunks));
    send_next_chunk();
}

void MainWindow::on_chunk_error() {
    log_error(QString("数据块 %1/%2 发送超时").arg(m_firmware_chunk_index).arg(m_firmware_total_chunks));
    reset_upgrade_state();
}

void MainWindow::on_upgrade_result(const ControlFrame& frame) {
    if (frame.msg_type != MSG_TYPE_RESPONSE) { on_upgrade_error(); return; }
    auto data = frame.data;
    log(QString("← 远程更新结果应答: data=%1").arg(QString(data.toHex())));

    if (data.size() >= 1) {
        uint8_t status = static_cast<uint8_t>(data[0]);
        if (status == UPDATE_STATUS_SUCCESS) {
            m_progress_firmware->setValue(100);
            m_lbl_upgrade_status->setText("升级成功！");
            log("=== 固件升级成功 ===");
            QMessageBox::information(this, "升级完成", "固件升级成功！");
        } else if (status == UPDATE_STATUS_IN_PROGRESS) {
            log("升级仍在进行中，等待后重新查询...");
            m_protocol->send_remote_update_result(
                [this](const ControlFrame& r) { on_upgrade_result(r); },
                [this]() { on_upgrade_error(); });
            return;
        } else {
            m_lbl_upgrade_status->setText(QString("升级失败 (状态码: 0x%1)").arg(status, 2, 16, QChar('0')));
            log_error(QString("固件升级失败: 状态码 0x%1").arg(status, 2, 16, QChar('0')));
            QMessageBox::warning(this, "升级失败",
                QString("固件升级失败，状态码: 0x%1").arg(status, 2, 16, QChar('0')));
        }
    } else {
        log_error("升级结果数据为空");
        reset_upgrade_state();
        return;
    }
    reset_upgrade_state();
}

void MainWindow::on_upgrade_error() {
    log_error("固件升级流程出错，已中止");
    m_lbl_upgrade_status->setText("升级失败，请重试");
    QMessageBox::critical(this, "升级错误", "固件升级过程中发生错误，请检查连接后重试。");
    reset_upgrade_state();
}

void MainWindow::reset_upgrade_state() {
    m_upgrade_in_progress = false;
    m_firmware_chunks.clear();
    m_firmware_chunk_index = 0;
    m_firmware_total_chunks = 0;
    set_upgrade_ui_enabled(true);
}

void MainWindow::set_upgrade_ui_enabled(bool enabled) {
    m_btn_browse_firmware->setEnabled(enabled);
    m_btn_upgrade_start->setEnabled(enabled && !m_firmware_path.isEmpty());
    if (!enabled) m_btn_upgrade_start->setEnabled(false);
}

// ══════════════════════════════════════════════════
// Misc handlers
// ══════════════════════════════════════════════════

void MainWindow::on_tab_changed(int index) {
    if (index == 2) {
        m_cam_selector_group->setEnabled(false);
        m_cam_selector_group->setTitle("相机选择 (系统模式下不可用)");
        m_cam_selector_group->setToolTip("系统模式下无法选择相机，请切换到触发或图像页签");
        auto* effect = new QGraphicsOpacityEffect(m_cam_selector_group);
        effect->setOpacity(0.4);
        m_cam_selector_group->setGraphicsEffect(effect);
    } else {
        m_cam_selector_group->setEnabled(true);
        m_cam_selector_group->setTitle("相机选择");
        m_cam_selector_group->setToolTip("");
        m_cam_selector_group->setGraphicsEffect(nullptr);
    }
}

void MainWindow::on_save_image() {
    QPixmap pixmap = active_view()->pixmap();
    if (pixmap.isNull()) {
        log_error("没有图像可保存");
        return;
    }
    QString save_dir = QCoreApplication::applicationDirPath() + "/captured_images";
    QDir().mkpath(save_dir);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString filename = QString("capture_%1.png").arg(timestamp);
    if (pixmap.save(save_dir + "/" + filename)) {
        log(QString("图像已保存: %1").arg(filename));
    } else {
        log_error("图像保存失败");
    }
}

void MainWindow::on_open_protocol_analyzer() {
    auto* dlg = new ProtocolAnalyzerDialog(this);
    dlg->exec();
    dlg->deleteLater();
}

void MainWindow::on_sdk_test() {
    if (!m_protocol) { log_error("ProtocolHandler 未初始化，无法运行 SDK 测试"); return; }
    if (m_sdk_test && m_sdk_test->isRunning()) {
        log("SDK 测试已在运行中，忽略本次点击");
        return;
    }

    // Mock 模式（mockMode=true）：无 FPGA 环境下验证 SDK 数据链路/设备信息/句柄管理，
    // 控制命令预期 CAM_ERR_TIMEOUT。改为 false 即真实 FPGA 模式。
    log("════════ 开始 SDK 功能测试（MOCK 模式，后台线程执行）════════");
    auto* t = new SDKTest(m_protocol, m_config, true);
    m_sdk_test = t;
    connect(t, &SDKTest::log, this, [this](const QString& line) { log(line); });
    connect(t, &QThread::finished, this, [this, t]() {
        if (m_sdk_test == t) m_sdk_test = nullptr;
        t->deleteLater();
    });
    t->start();
}

void MainWindow::on_load_test() {
    // Test data loading — same as Python _on_load_test
    QString assets_dir = QCoreApplication::applicationDirPath();
    QString test_data_dir = assets_dir + "/../fpga_sf_test/data";
    QString test_output_dir = assets_dir + "/../fpga_sf_test/output";

    QDir base(assets_dir);
    base.cdUp();

    // Find test data directory
    QString img_path = base.absolutePath() + "/fpga_sf_test/data/standard.bmp";
    QString center_path = base.absolutePath() + "/fpga_sf_test/output/center_points.txt";
    QString pc_path = base.absolutePath() + "/fpga_sf_test/output/pointcloud.txt";

    if (QFile::exists(img_path)) {
        cv::Mat img = cv::imread(img_path.toStdString(), cv::IMREAD_GRAYSCALE);
        if (!img.empty()) {
            for (auto* view : m_image_views)
                view->set_image(img);
            log(QString("已加载测试图像: %1").arg(img_path));
        }
    }
    if (QFile::exists(center_path)) {
        QVector<QPair<float, float>> center_pts;
        QFile f(center_path);
        if (f.open(QIODevice::ReadOnly)) {
            while (!f.atEnd()) {
                QString line = QString::fromUtf8(f.readLine()).trimmed();
                auto parts = line.split(' ');
                if (parts.size() >= 2)
                    center_pts.append({parts[0].toFloat(), parts[1].toFloat()});
            }
        }
        if (!center_pts.isEmpty()) {
            for (auto* view : m_image_views)
                view->set_contour_image_points(center_pts);
            log(QString("已加载中心点: %1 个").arg(center_pts.size()));
        }
    }
    if (QFile::exists(pc_path)) {
        log(QString("点云文件存在: %1 (手动加载调用 _load_pointcloud_to_views)").arg(pc_path));
    }
}

// ══════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════

ImageView* MainWindow::active_view() {
    auto cams = visible_camera_indices();
    return m_image_views[cams[0]];
}

QVector<int> MainWindow::target_sensor_ids() const {
    if (m_selected_camera == 0)
        return {1, 2, 3, 4};
    return {m_selected_camera};
}

QString MainWindow::camera_label() const {
    if (m_selected_camera == 0) return "All";
    return QString("Cam%1").arg(m_selected_camera);
}

QStringList MainWindow::get_local_ips() {
    QStringList ips;
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto& addr : addresses) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            ips.append(addr.toString());
    }
    ips.sort();
    if (ips.isEmpty()) ips << "0.0.0.0";
    return ips;
}

void MainWindow::refresh_local_ips(QComboBox* combo) {
    QString current = combo->currentText();
    combo->clear();
    QStringList ips = get_local_ips();
    combo->addItems(ips);
    if (ips.contains(current))
        combo->setCurrentText(current);
    else if (!ips.isEmpty())
        combo->setCurrentText(ips[0]);
}

void MainWindow::log(const QString& msg) {
    LOG_INFO(msg.toUtf8().constData());
}

void MainWindow::log_error(const QString& msg) {
    LOG_ERROR(msg.toUtf8().constData());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    m_display_timer->stop();
    if (m_sdk_test && m_sdk_test->isRunning()) m_sdk_test->request_stop();
    m_protocol->stop_all();
    event->accept();
}
