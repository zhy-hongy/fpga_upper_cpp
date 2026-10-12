#include "main_window.hpp"
#include "fpga/upgrade.hpp"
#include "fpga/analysis.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>

namespace {
const char* theme = R"(
QWidget { background: #101722; color: #e0e8f4; font-family: 'Microsoft YaHei UI'; font-size: 12px; }
QMainWindow, QScrollArea, QScrollArea>QWidget>QWidget { background: #101722; }
QLabel#brand { color:#f3f7ff; font-size:23px; font-weight:700; }
QLabel#muted { color:#8393a9; }
QLabel#badge { background:#183739; color:#5fe1c9; border:1px solid #24504d; border-radius:5px; padding:5px 10px; }
QLabel#metric { color:#eaf3ff; font-size:21px; font-weight:600; }
QFrame#card { background:#182333; border:1px solid #263348; border-radius:8px; }
QLabel#metric, QFrame#card QLabel { background:transparent; }
QGroupBox { border:1px solid #2b3a50; border-radius:7px; margin-top:20px; padding:12px 10px 8px; font-weight:600; }
QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:#b4c6dd; }
QPushButton { background:#24334a; border:1px solid #34465e; border-radius:5px; padding:7px 13px; }
QPushButton:hover { background:#30435e; border-color:#6b93ba; }
QPushButton:pressed { background:#142d46; }
QPushButton:checked, QPushButton#primary { background:#178c82; border-color:#32b4a7; color:#fff; }
QPushButton#danger { color:#ffa59f; border-color:#704148; }
QPushButton:disabled { color:#657286; background:#1a2331; border-color:#283345; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background:#0d1420; border:1px solid #344259; border-radius:4px; padding:6px; selection-background-color:#247f94; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color:#45b7ba; }
QComboBox QAbstractItemView { background:#182333; border:1px solid #384b64; selection-background-color:#28506b; }
QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button { background:#253149; width:16px; }
QTabWidget::pane { border:1px solid #2a3b50; border-radius:6px; }
QTabBar::tab { background:#192536; color:#8599b3; padding:9px 11px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { color:#73e1d5; background:#213347; border-bottom:2px solid #41c7b7; }
QPlainTextEdit, QTableWidget { background:#0d1420; border:1px solid #26364c; border-radius:4px; gridline-color:#223148; selection-background-color:#284a63; }
QHeaderView::section { background:#1e2b3f; color:#9eb3cd; padding:7px 4px; border:0; border-bottom:1px solid #33435b; }
QTableWidget::item { padding:5px; border-bottom:1px solid #1b2738; }
QScrollBar:vertical { background:#111b29; width:9px; margin:0; }
QScrollBar::handle:vertical { background:#3b4e66; border-radius:4px; min-height:20px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }
QScrollBar:horizontal { background:#111b29; height:9px; }
QScrollBar::handle:horizontal { background:#3b4e66; border-radius:4px; min-width:20px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; }
QProgressBar { background:#182b3c; border:1px solid #2a4659; border-radius:4px; text-align:center; height:18px; }
QProgressBar::chunk { background:#259d91; }
QCheckBox::indicator { width:14px; height:14px; border:1px solid #597188; background:#111b29; border-radius:3px; }
QCheckBox::indicator:checked { background:#36c4b2; border-color:#64ddce; }
QStatusBar { background:#0d1320; color:#a6bacf; border-top:1px solid #25364b; }
QSplitter::handle { background:#172233; }
)";

QPushButton* button(const QString& text, QLayout* layout, bool primary = false) {
    auto b = new QPushButton(text);
    if (primary) b->setObjectName("primary");
    layout->addWidget(b);
    return b;
}
QSpinBox* spin(int lo, int hi, int value) {
    auto s = new QSpinBox;
    s->setRange(lo, hi); s->setValue(value); s->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    return s;
}
QDoubleSpinBox* decimal(double lo, double hi, double value, int digits = 5) {
    auto s = new QDoubleSpinBox;
    s->setDecimals(digits); s->setRange(lo, hi); s->setValue(value);
    s->setButtonSymbols(QAbstractSpinBox::PlusMinus); return s;
}
QWidget* make_scroll(QWidget* inner) {
    auto area = new QScrollArea;
    area->setWidgetResizable(true); area->setFrameShape(QFrame::NoFrame); area->setWidget(inner);
    return area;
}
QString hex(const fpga::Bytes& bytes) {
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())).toHex(' ').toUpper();
}
fpga::Bytes fromHex(const QString& text) {
    QString clean = text; clean.remove(QRegularExpression("\\s+"));
    if (clean.size() % 2 || clean.contains(QRegularExpression("[^0-9a-fA-F]")))
        throw std::invalid_argument("HEX 必须是成对的十六进制字节");
    auto a = QByteArray::fromHex(clean.toLatin1());
    return fpga::Bytes(a.begin(), a.end());
}
QString chineseName(uint16_t cmd) {
    static const std::map<uint16_t, QString> names = {
        {1,"曝光 / μs"},{2,"增益"},{3,"X ROI 起点, lane 数"},{4,"Z ROI 起点, 宽度"},
        {5,"CMOS 区域 X1,X2,Y1,Y2"},{6,"四路曝光延迟 / 20 ns"},{7,"相机固件版本"},
        {8,"相机位置编号"},{9,"相机序列号"},{10,"采样频率 / Hz"},{11,"电源 0开/1关"},
        {12,"图像模式 0正常/1测试"},{13,"行发送间隔"},{14,"激光 0关/1开"},
        {15,"激光亮度 / %"},{16,"激光提前, 延迟 / 20 ns"},{17,"帧等待时间"},
        {18,"HDR 拐点 1, 2"},{19,"HDR 长曝光, 短曝光"},
        {0x101,"触发模式 1连续/2编码/3外部"},{0x102,"编码器类型 1差分/2单端"},
        {0x103,"编码器输入模式"},{0x104,"编码器间隔"},{0x201,"批处理长度"},
        {0x202,"外部触发使能"},{0x203,"批控制"},{0x207,"3D 插值配置"},
        {0x501,"网络参数"},{0x502,"FPGA 固件版本"},{0x503,"设备序列号"},
        {0x504,"相机型号"},{0x505,"标定数据 / 384B"},{0x506,"设备型号"}
    };
    auto i = names.find(cmd); return i == names.end() ? QString("0x%1").arg(cmd,4,16,QChar('0')) : i->second;
}
QString defaultValue(uint16_t cmd) {
    switch(cmd) {
    case 1:return "5000"; case 2:return "1"; case 3:return "0, 16"; case 4:return "0, 1152";
    case 5:return "0, 2047, 0, 1151"; case 6:return "0, 0, 0, 0"; case 10:return "1500";
    case 13:return "4500"; case 15:return "50"; case 16:return "0, 0"; case 17:return "1000";
    case 18:return "50, 100"; case 19:return "5000, 1000";
    case 0x101:return "3"; case 0x102:case 0x103:case 0x104:return "1"; case 0x201:return "50";
    case 0x504:case 0x506:return "FPGA-CAM"; case 0x207:return "-10, 10, 0.1, 200";
    default:return "0";
    }
}
}

class FrameView final : public QWidget {
public:
    explicit FrameView(int index) : index_(index) { setMinimumSize(200,150); }
    void setFrame(QImage image, uint64_t sequence) { image_ = std::move(image); sequence_ = sequence; update(); }
    const QImage& image() const { return image_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor("#2a3b50")); p.setBrush(QColor("#0b111c"));
        p.drawRoundedRect(rect().adjusted(1,1,-2,-2),7,7);
        QRect area = rect().adjusted(9,40,-9,-29);
        if (!image_.isNull()) {
            QSize size = image_.size().scaled(area.size(),Qt::KeepAspectRatio);
            QRect dst(QPoint(area.center().x()-size.width()/2,area.center().y()-size.height()/2),size);
            p.drawImage(dst,image_);
            p.setPen(QPen(QColor(58,221,190,100),1,Qt::DashLine));
            p.drawLine(dst.left(),dst.center().y(),dst.right(),dst.center().y());
            p.drawLine(dst.center().x(),dst.top(),dst.center().x(),dst.bottom());
        } else {
            p.setPen(QColor("#62728a")); p.drawText(area,Qt::AlignCenter,"等待设备图像\n连接后启动采集");
        }
        p.setPen(QColor("#5cddca")); p.setBrush(QColor("#39cfb6")); p.drawEllipse(QPoint(18,21),3,3);
        p.setFont(QFont("Microsoft YaHei UI",10,QFont::DemiBold));
        p.drawText(QRect(31,7,width()-90,25),Qt::AlignVCenter,QString("CAM %1").arg(index_));
        p.setFont(QFont("Microsoft YaHei UI",8)); p.setPen(QColor("#7389a3"));
        p.drawText(QRect(10,height()-27,width()-20,20),Qt::AlignVCenter,
            image_.isNull()?"GRAY8  ·  未接收":QString("%1 × %2  ·  GRAY8  ·  帧 %3").arg(image_.width()).arg(image_.height()).arg(sequence_));
    }
private:
    int index_; uint64_t sequence_=0; QImage image_;
};

class ProfileView final : public QWidget {
public:
    ProfileView() { setMinimumHeight(165); }
    void setPoints(std::vector<QPointF> values, const QString& caption) { points_=std::move(values); caption_=caption; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(),QColor("#0b1421"));
        QRectF area = rect().adjusted(53,24,-24,-30);
        p.setPen(QColor("#26374d"));
        for(int i=0;i<=4;++i) { double y=area.top()+area.height()*i/4; p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y)); }
        for(int i=0;i<=8;++i) { double x=area.left()+area.width()*i/8; p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom())); }
        p.setPen(QColor("#8ea2bf")); p.drawText(QRect(13,1,width()-26,22),Qt::AlignVCenter,caption_);
        p.drawText(QRect(10,height()-22,width()-20,20),Qt::AlignRight,"X / 采样点");
        if(points_.empty()) { p.drawText(area,Qt::AlignCenter,"轮廓 / 点云 / 批数据将在接收后显示"); return; }
        double xmin=points_[0].x(),xmax=xmin,ymin=points_[0].y(),ymax=ymin;
        for(const auto& v:points_) { xmin=std::min(xmin,v.x());xmax=std::max(xmax,v.x()); ymin=std::min(ymin,v.y());ymax=std::max(ymax,v.y()); }
        if(xmax==xmin) xmax=xmin+1; if(ymax==ymin) ymax=ymin+1;
        double span=ymax-ymin; ymin-=span*.12;ymax+=span*.12;
        QPainterPath path;
        for(size_t i=0;i<points_.size();++i) {
            QPointF v(area.left()+(points_[i].x()-xmin)/(xmax-xmin)*area.width(),area.bottom()-(points_[i].y()-ymin)/(ymax-ymin)*area.height());
            if(!i)path.moveTo(v);else path.lineTo(v);
        }
        p.setPen(QPen(QColor("#4cddc4"),1.7));p.drawPath(path);
        p.setPen(QColor("#8ca2c1"));
        p.drawText(QRect(3,static_cast<int>(area.top())-7,46,18),Qt::AlignRight,QString::number(ymax,'g',4));
        p.drawText(QRect(3,static_cast<int>(area.bottom())-7,46,18),Qt::AlignRight,QString::number(ymin,'g',4));
        p.drawText(QRect(static_cast<int>(area.left()),height()-26,90,20),QString::number(xmin,'g',4));
        p.drawText(QRect(static_cast<int>(area.right())-100,height()-26,100,20),Qt::AlignRight,QString::number(xmax,'g',4));
    }
private:
    std::vector<QPointF> points_; QString caption_="实时轮廓";
};

MainWindow::MainWindow(QWidget* parent):QMainWindow(parent) {
    for(auto&pending:renderPending_)pending.store(false);
    setWindowTitle("FPGA Studio — C++ SDK / Qt Demo");resize(1500,990);setMinimumSize(1100,740);
    setStyleSheet(theme);buildUi();
    worker_ = std::thread([this] {
        for(;;) { std::function<void()> job;
            { std::unique_lock<std::mutex> lock(queueMutex_);queueCv_.wait(lock,[this]{return workerStop_||!jobs_.empty();});
              if(workerStop_) return;job=std::move(jobs_.front());jobs_.pop_front(); }
            try {job();} catch(const std::exception& e) { QString m=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,m]{log(m,true);},Qt::QueuedConnection); }
        }
    });
    device_.on_camera([this](std::shared_ptr<const fpga::CameraEvent> e) {
        if(!e||e->device<1||e->device>4)return;const auto sensor=e->device;
        if(renderPending_[sensor-1].exchange(true))return;
        QMetaObject::invokeMethod(this,[this,sensor]{renderPending_[sensor-1].store(false);receiveCamera(device_.latest_frame(sensor));},Qt::QueuedConnection);
    });
    device_.on_response([this](const fpga::Response&r){if(r&&r.frame.command==0x401){const auto data=r.frame.data;QMetaObject::invokeMethod(this,[this,data]{lastHeartbeatMs_=QDateTime::currentMSecsSinceEpoch();if(connected_){connectionStatus_->setText("●  设备在线");connectionStatus_->setStyleSheet("color:#55dfc4;");connectionStatus_->setToolTip("最新心跳状态: "+hex(data));}},Qt::QueuedConnection);}});
    device_.on_batch([this](const fpga::BatchEvent& e) { QMetaObject::invokeMethod(this,[this,e]{receiveBatch(e);},Qt::QueuedConnection); });
    device_.on_error([this](const std::string& m) { QString value=QString::fromStdString(m);QMetaObject::invokeMethod(this,[this,value]{log(value,true);},Qt::QueuedConnection); });
    statsTimer_=new QTimer(this);statsTimer_->setInterval(500);connect(statsTimer_,&QTimer::timeout,this,[this]{updateStatistics();});statsTimer_->start();
    log("C++ SDK 已就绪。选择 UDP 模拟设备可进行完整通信演示。");
}
MainWindow::~MainWindow() {
    statsTimer_->stop();
    upgradeCancelled_=true;
    {std::lock_guard<std::mutex> lock(queueMutex_);workerStop_=true;jobs_.clear();}
    {std::lock_guard<std::mutex> lock(lifecycleMutex_);device_.close();}
    queueCv_.notify_all();if(worker_.joinable())worker_.join();device_.close();simulator_.stop();
}
void MainWindow::buildUi() {
    auto root=new QWidget;setCentralWidget(root);auto layout=new QVBoxLayout(root);layout->setContentsMargins(20,16,20,12);layout->setSpacing(12);
    auto header=new QHBoxLayout;auto title=new QLabel("FPGA STUDIO");title->setObjectName("brand");header->addWidget(title);
    auto badge=new QLabel("C++ SDK 2.0");badge->setObjectName("badge");header->addWidget(badge);header->addSpacing(10);
    auto subtitle=new QLabel("四路视觉采集与设备控制");subtitle->setObjectName("muted");header->addWidget(subtitle);header->addStretch();
    connectionStatus_=new QLabel("●  未连接");connectionStatus_->setStyleSheet("color:#94a5ba;");header->addWidget(connectionStatus_);layout->addLayout(header);
    auto connection=new QHBoxLayout;mode_=new QComboBox;mode_->addItems({"UDP 模拟设备","真实 FPGA"});mode_->setMinimumWidth(144);connection->addWidget(mode_);
    connection->addWidget(new QLabel("设备"));deviceIp_=new QLineEdit("192.168.10.10");deviceIp_->setMaximumWidth(155);connection->addWidget(deviceIp_);devicePort_=spin(1,65535,60000);devicePort_->setMaximumWidth(96);connection->addWidget(devicePort_);
    connection->addWidget(new QLabel("本机"));localIp_=new QLineEdit("0.0.0.0");localIp_->setMaximumWidth(140);connection->addWidget(localIp_);localPort_=spin(0,65535,60000);localPort_->setMaximumWidth(96);connection->addWidget(localPort_);
    connectButton_=button("连接设备",connection,true);connect(connectButton_,&QPushButton::clicked,this,[this]{connected_?disconnectDevice():connectDevice();});connection->addStretch();
    captureButton_=button("启动采集",connection,true);captureButton_->setCheckable(true);connect(captureButton_,&QPushButton::clicked,this,[this](bool enabled){capture(enabled);});
    auto single=button("单帧",connection);connect(single,&QPushButton::clicked,this,[this]{singleCapture();});auto save=button("导出数据",connection);connect(save,&QPushButton::clicked,this,[this]{exportCurrentData();});layout->addLayout(connection);
    auto metrics=new QHBoxLayout;
    auto metric=[&](const QString& caption,QLabel*& value,const QString& suffix){auto card=new QFrame;card->setObjectName("card");auto l=new QVBoxLayout(card);l->setContentsMargins(16,9,16,9);auto top=new QLabel(caption);top->setObjectName("muted");l->addWidget(top);value=new QLabel(suffix);value->setObjectName("metric");l->addWidget(value);metrics->addWidget(card);};
    metric("接收图像 · 四路",frameMetric_,"0 帧");metric("数据吞吐 · UDP",trafficMetric_,"0.00 MB");metric("协议完整性 · CRC / 分包",integrityMetric_,"0 异常");layout->addLayout(metrics);
    auto split=new QSplitter(Qt::Horizontal);layout->addWidget(split,1);auto displays=new QWidget;auto dl=new QVBoxLayout(displays);dl->setContentsMargins(0,0,0,0);dl->setSpacing(10);
    auto grid=new QGridLayout;grid->setSpacing(10);for(int i=0;i<4;++i){frames_[i]=new FrameView(i+1);grid->addWidget(frames_[i],i/2,i%2);}dl->addLayout(grid,3);
    auto plotHeader=new QHBoxLayout;auto ptitle=new QLabel("数据视图");ptitle->setStyleSheet("font-weight:600;");plotHeader->addWidget(ptitle);plotHeader->addStretch();plotMode_=new QComboBox;plotMode_->addItems({"中心线 / 轮廓","3D 点云 · X-Z","3D 插值","批处理轮廓"});plotHeader->addWidget(plotMode_);dl->addLayout(plotHeader);
    profile_=new ProfileView;dl->addWidget(profile_,1);connect(plotMode_,&QComboBox::currentIndexChanged,this,[this]{auto f=lastFrames_[selectedCamera()-1];if(f)receiveCamera(f);});split->addWidget(displays);
    auto panel=new QWidget;panel->setMinimumWidth(386);panel->setMaximumWidth(520);auto pl=new QVBoxLayout(panel);pl->setContentsMargins(7,0,0,0);pl->setSpacing(10);auto selector=new QHBoxLayout;selector->addWidget(new QLabel("操作目标"));target_=new QComboBox;target_->addItems({"全部相机","CAM 1","CAM 2","CAM 3","CAM 4","FPGA 主控"});selector->addWidget(target_,1);pl->addLayout(selector);
    auto imageOptions=new QHBoxLayout;imageOptions->addWidget(new QLabel("图像请求"));imageRequest_=new QComboBox;imageRequest_->addItems({"FPGA 已重排","原始 · SDK 重排"});imageOptions->addWidget(imageRequest_,1);captureInterval_=spin(10,5000,40);captureInterval_->setSuffix(" ms");captureInterval_->setMaximumWidth(105);imageOptions->addWidget(captureInterval_);pl->addLayout(imageOptions);
    controls_=new QTabWidget;controls_->addTab(buildParameters(),"参数");controls_->addTab(make_scroll(buildTrigger()),"触发 / 批");controls_->addTab(make_scroll(buildInterpolation()),"插值");controls_->addTab(make_scroll(buildSystem()),"系统");controls_->addTab(make_scroll(buildRegisters()),"寄存器");controls_->addTab(make_scroll(buildAnalysis()),"算法");pl->addWidget(controls_,1);split->addWidget(panel);split->setSizes({1000,420});
    connect(target_,&QComboBox::currentIndexChanged,this,[this]{rebuildParameters();});rebuildParameters();
    auto logHeader=new QHBoxLayout;auto ltitle=new QLabel("通信日志");ltitle->setStyleSheet("font-weight:600;");logHeader->addWidget(ltitle);auto hint=new QLabel("控制请求在工作线程执行 · 图像与控制共用 SDK");hint->setObjectName("muted");logHeader->addSpacing(14);logHeader->addWidget(hint);logHeader->addStretch();auto clear=button("清空",logHeader);connect(clear,&QPushButton::clicked,this,[this]{logs_->clear();});layout->addLayout(logHeader);
    logs_=new QPlainTextEdit;logs_->setReadOnly(true);logs_->setMaximumBlockCount(500);logs_->setFixedHeight(104);logs_->setFont(QFont("Consolas",9));layout->addWidget(logs_);
    bottomStatus_=new QLabel("准备就绪");statusBar()->addWidget(bottomStatus_,1);auto sdk=new QLabel("Qt 6 Widgets  /  C++17  /  UDP SDK");sdk->setObjectName("muted");statusBar()->addPermanentWidget(sdk);
}

QWidget* MainWindow::buildParameters() {
    auto page=new QWidget;auto l=new QVBoxLayout(page);l->setContentsMargins(9,10,9,9);auto note=new QLabel("编辑值与设备确认值分开显示。支持全部协议参数。\n多字段参数用逗号分隔；ROI 使用起点和数量。");note->setWordWrap(true);note->setObjectName("muted");l->addWidget(note);
    parameters_=new QTableWidget(0,3);parameters_->setHorizontalHeaderLabels({"参数","编辑值","确认值"});parameters_->verticalHeader()->hide();parameters_->setSelectionBehavior(QAbstractItemView::SelectRows);parameters_->setSelectionMode(QAbstractItemView::SingleSelection);parameters_->setColumnWidth(0,152);parameters_->setColumnWidth(1,100);parameters_->horizontalHeader()->setStretchLastSection(true);parameters_->setWordWrap(false);l->addWidget(parameters_,1);
    auto actions=new QHBoxLayout;auto read=button("读取选中",actions);connect(read,&QPushButton::clicked,this,[this]{readParameters(false);});auto write=button("写入选中",actions,true);connect(write,&QPushButton::clicked,this,[this]{writeParameter();});l->addLayout(actions);
    auto all=new QHBoxLayout;auto refresh=button("回读全部参数",all);connect(refresh,&QPushButton::clicked,this,[this]{readParameters(true);});l->addLayout(all);return page;
}

QWidget* MainWindow::buildTrigger() {
    auto page=new QWidget;auto l=new QVBoxLayout(page);l->setContentsMargins(10,5,10,12);
    auto group=new QGroupBox("触发与采样");auto form=new QFormLayout(group);triggerMode_=new QComboBox;triggerMode_->addItems({"连续触发","编码器触发","外部触发"});triggerMode_->setCurrentIndex(2);form->addRow("触发模式",triggerMode_);encoderType_=new QComboBox;encoderType_->addItems({"差分","单端"});form->addRow("编码器类型",encoderType_);encoderMode_=new QComboBox;encoderMode_->addItems({"2相4递增","2相2递增","2相1递增"});form->addRow("输入模式",encoderMode_);encoderInterval_=spin(1,10000000,1);form->addRow("编码器间隔",encoderInterval_);sampleRate_=decimal(.001,50000000,1500,3);sampleRate_->setSuffix(" Hz");form->addRow("采样频率",sampleRate_);
    auto set= new QPushButton("下发触发配置");set->setObjectName("primary");form->addRow(set);connect(set,&QPushButton::clicked,this,[this]{int m=triggerMode_->currentIndex()+1,t=encoderType_->currentIndex()+1,em=encoderMode_->currentIndex()+1,gap=encoderInterval_->value();double hz=sampleRate_->value();submit("触发配置",[this,m,t,em,gap,hz]{for(auto pair:std::vector<std::pair<uint16_t,uint32_t>>{{0x101,uint32_t(m)},{0x102,uint32_t(t)},{0x103,uint32_t(em)},{0x104,uint32_t(gap)}}){auto r=device_.write_u32(0,pair.first,pair.second);if(!r)return r;}return device_.set_sample_rate(hz);});});l->addWidget(group);
    auto delayGroup=new QGroupBox("四路曝光延迟 / 20 ns 计数");auto delayForm=new QFormLayout(delayGroup);for(int i=0;i<4;++i){delays_[i]=spin(0,1000000000,0);delayForm->addRow(QString("CAM %1").arg(i+1),delays_[i]);}auto delaySet=new QPushButton("下发延迟");delayForm->addRow(delaySet);connect(delaySet,&QPushButton::clicked,this,[this]{std::array<uint32_t,4> values{};for(int i=0;i<4;++i)values[i]=delays_[i]->value();submit("曝光延迟",[this,values]{return device_.set_exposure_delay(values);});});l->addWidget(delayGroup);
    auto batch=new QGroupBox("批处理与缓存");auto bl=new QVBoxLayout(batch);auto bf=new QFormLayout;batchLength_=spin(1,1000000,50);bf->addRow("批长度",batchLength_);batchIndex_=spin(0,65535,0);bf->addRow("批索引",batchIndex_);bl->addLayout(bf);auto countSet=button("下发批长度 · 主控与四路",bl);connect(countSet,&QPushButton::clicked,this,[this]{auto n=batchLength_->value();submit("批长度",[this,n]{return device_.set_batch_length(n);});});auto action=new QHBoxLayout;auto start=button("开始",action,true);auto stop=button("停止",action);auto reset=button("重置缓存",action);bl->addLayout(action);
    connect(start,&QPushButton::clicked,this,[this]{int index=batchIndex_->value();submit("开始批处理",[this,index]{return device_.set_batch_control(true,index);},[this](const fpga::Response&){batchStatus_->setText("● 批处理运行中");});});connect(stop,&QPushButton::clicked,this,[this]{submit("停止批处理",[this]{return device_.set_batch_control(false);},[this](const fpga::Response&){batchStatus_->setText("● 批处理已停止");});});connect(reset,&QPushButton::clicked,this,[this]{auto targets=cameraTargets();submit("重置缓存",[this,targets]{fpga::Response r;for(auto t:targets){r=device_.reset_buffer(t);if(!r)return r;}return r;},[this](const fpga::Response&){batchCount_=0;batchStatus_->setText("● 缓存已重置");});});
    auto fetch=new QHBoxLayout;auto realtime=button("获取实时批",fetch);auto all=button("获取全部批",fetch);bl->addLayout(fetch);auto get=[this](bool all){auto targets=cameraTargets();submit(all?"获取全部批":"获取实时批",[this,targets,all]{fpga::Response r;for(auto t:targets){r=device_.get_batch_data(t,all);if(!r)return r;}return r;});};connect(realtime,&QPushButton::clicked,this,[get]{get(false);});connect(all,&QPushButton::clicked,this,[get]{get(true);});batchStatus_=new QLabel("● 批处理空闲");batchStatus_->setStyleSheet("color:#50d1c0;");bl->addWidget(batchStatus_);l->addWidget(batch);l->addStretch();return page;
}

QWidget* MainWindow::buildInterpolation() {
    auto page=new QWidget;auto l=new QVBoxLayout(page);l->setContentsMargins(10,5,10,12);auto group=new QGroupBox("3D 插值 · signed Q16.16");auto f=new QFormLayout(group);interpStart_=decimal(-32768,32767,-10,6);interpEnd_=decimal(-32768,32767,10,6);interpInterval_=decimal(.000015258789,32767,.1,8);interpCount_=spin(1,2048,200);f->addRow("起点 / mm",interpStart_);f->addRow("上界 / mm",interpEnd_);f->addRow("间隔 / mm",interpInterval_);f->addRow("点数",interpCount_);
    auto autoStep=new QPushButton("按右开范围计算间隔");f->addRow(autoStep);connect(autoStep,&QPushButton::clicked,this,[this]{interpInterval_->setValue((interpEnd_->value()-interpStart_->value())/interpCount_->value());});l->addWidget(group);interpPreview_=new QLabel;interpPreview_->setWordWrap(true);interpPreview_->setStyleSheet("background:#152939;color:#77ddce;padding:12px;border-radius:5px;");l->addWidget(interpPreview_);
    for(auto s:{interpStart_,interpEnd_,interpInterval_})connect(s,&QDoubleSpinBox::valueChanged,this,[this]{updateInterpolationPreview();});connect(interpCount_,&QSpinBox::valueChanged,this,[this]{updateInterpolationPreview();});
    auto actions=new QHBoxLayout;auto set=button("写入并回读",actions,true);auto read=button("读取配置",actions);l->addLayout(actions);
    connect(set,&QPushButton::clicked,this,[this]{try{auto c=fpga::InterpolationConfig::from_mm(interpStart_->value(),interpEnd_->value(),interpInterval_->value(),interpCount_->value());auto targets=cameraTargets();submit("插值配置",[this,c,targets]{fpga::Response r;for(auto t:targets){r=device_.write(t,0x207,fpga::pack_interpolation_config(c));if(!r)return r;r=device_.read(t,0x207);if(!r)return r;if(r.frame.data!=fpga::pack_interpolation_config(c))throw std::runtime_error("插值配置回读不一致");}return r;});}catch(const std::exception&e){log(QString::fromUtf8(e.what()),true);}});
    connect(read,&QPushButton::clicked,this,[this]{auto t=selectedCamera();submit("读取插值配置",[this,t]{return device_.read(t,0x207);},[this](const fpga::Response&r){auto c=fpga::unpack_interpolation_config(r.frame.data);interpStart_->setValue(c.start_mm());interpEnd_->setValue(c.end_mm());interpInterval_->setValue(c.interval_mm());interpCount_->setValue(c.count);});});
    auto get=button("获取 3D 插值结果",l);connect(get,&QPushButton::clicked,this,[this]{auto t=selectedCamera();plotMode_->setCurrentIndex(2);submit("获取插值结果",[this,t]{return device_.read(t,0x304);});});auto explanation=new QLabel("起点、上界与间隔各占 4 字节，点数为 uint32。发送前由 SDK 完成量化和范围校验。界面显示实际末点及量化后间隔。");explanation->setWordWrap(true);explanation->setObjectName("muted");l->addWidget(explanation);l->addStretch();updateInterpolationPreview();return page;
}

QWidget* MainWindow::buildSystem() {
    auto page=new QWidget;auto l=new QVBoxLayout(page);l->setContentsMargins(10,5,10,12);
    auto identity=new QGroupBox("设备与标定");auto il=new QVBoxLayout(identity);auto versions=button("读取 FPGA / 四路版本与型号",il);connect(versions,&QPushButton::clicked,this,[this]{enqueue([this]{for(uint16_t t=0;t<=4;++t)for(uint16_t c:std::vector<uint16_t>{uint16_t(t?7:0x502),uint16_t(t?0x504:0x506)}){auto r=device_.read(t,c);QString m=QString("设备 %1 %2: %3").arg(t).arg(chineseName(c)).arg(r?QString::fromUtf8(reinterpret_cast<const char*>(r.frame.data.data()),r.frame.data.size()):QString::fromStdString(r.error));QMetaObject::invokeMethod(this,[this,m,r]{log(m,!r);},Qt::QueuedConnection);}});});
    auto calib=button("加载标定 XML · 写入并回读",il);connect(calib,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"选择 neural_net.xml",{},"标定 XML (*.xml)");if(path.isEmpty())return;auto targets=cameraTargets();submit("标定数据",[this,path,targets]{auto data=fpga::pack_calibration_xml(path.toStdString());fpga::Response r;for(auto t:targets){r=device_.write(t,0x505,data);if(!r)return r;r=device_.read(t,0x505);if(!r)return r;if(r.frame.data!=data)throw std::runtime_error("标定数据回读不一致");}return r;});});l->addWidget(identity);
    auto net=new QGroupBox("设备网络参数");auto nf=new QFormLayout(net);const QStringList labels={"设备 IP","目标主机 IP","掩码","网关","MAC"};const QStringList values={"192.168.10.10","192.168.10.100","255.255.255.0","192.168.10.1","02:00:00:00:00:01"};for(int i=0;i<5;++i){network_[i]=new QLineEdit(values[i]);nf->addRow(labels[i],network_[i]);}auto nbuttons=new QHBoxLayout;auto nr=button("读取",nbuttons);auto nw=button("写入",nbuttons);nf->addRow(nbuttons);
    connect(nr,&QPushButton::clicked,this,[this]{submit("读取网络参数",[this]{return device_.read(0,0x501);},[this](const fpga::Response&r){auto n=fpga::unpack_network_parameters(r.frame.data);network_[0]->setText(QString::fromStdString(n.device_ip));network_[1]->setText(QString::fromStdString(n.host_ip));network_[2]->setText(QString::fromStdString(n.mask));network_[3]->setText(QString::fromStdString(n.gateway));fpga::Bytes b(n.mac.begin(),n.mac.end());network_[4]->setText(hex(b).replace(' ',':'));});});
    connect(nw,&QPushButton::clicked,this,[this]{try{fpga::NetworkParameters n;n.device_ip=network_[0]->text().toStdString();n.host_ip=network_[1]->text().toStdString();n.mask=network_[2]->text().toStdString();n.gateway=network_[3]->text().toStdString();auto b=fromHex(QString(network_[4]->text()).remove(':').remove('-'));if(b.size()!=6)throw std::invalid_argument("MAC 应为 6 字节");std::copy(b.begin(),b.end(),n.mac.begin());auto data=fpga::pack_network_parameters(n);submit("写入网络参数",[this,data]{return device_.write(0,0x501,data);});}catch(const std::exception&e){log(QString::fromUtf8(e.what()),true);}});l->addWidget(net);
    auto fw=new QGroupBox("远程固件升级");auto fl=new QVBoxLayout(fw);auto fileRow=new QHBoxLayout;firmwarePath_=new QLineEdit;firmwarePath_->setPlaceholderText("选择 .bin / .fpac 固件");fileRow->addWidget(firmwarePath_,1);auto browse=button("选择",fileRow);connect(browse,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"选择固件",{},"固件文件 (*.bin *.fpac);;全部文件 (*)");if(!p.isEmpty())firmwarePath_->setText(p);});fl->addLayout(fileRow);auto ff=new QFormLayout;firmwareTarget_=new QComboBox;firmwareTarget_->addItems({"主 FPGA","CAM 1","CAM 2","CAM 3","CAM 4"});ff->addRow("升级目标",firmwareTarget_);firmwarePort_=spin(1,65535,8888);ff->addRow("升级 UDP 端口",firmwarePort_);fl->addLayout(ff);firmwareProgress_=new QProgressBar;fl->addWidget(firmwareProgress_);auto upgrade=button("开始升级",fl,true);
    firmwareAddress_=new QLineEdit("0x00000000");ff->addRow("Flash 地址 / HEX",firmwareAddress_);
    auto cancel=button("取消升级",fl);connect(cancel,&QPushButton::clicked,this,[this]{upgradeCancelled_=true;});
    auto preview=button("解析固件 / 检查包",fl);
    connect(preview,&QPushButton::clicked,this,[this]{auto path=firmwarePath_->text();if(path.isEmpty()){log("请选择固件文件",true);return;}enqueue([this,path]{auto bytes=fpga::load_binary_file(path.toStdString());std::vector<fpga::FirmwareEntry> entries;if(path.endsWith(".fpac",Qt::CaseInsensitive))entries=fpga::parse_firmware_pack(bytes);else entries={{0,0,std::move(bytes)}};for(const auto&e:entries){QString m=QString("固件检查 ✓ · CHIP %1 · Flash 0x%2 · %3 B · CRC32 0x%4").arg(e.chip_id).arg(e.flash_addr,8,16,QChar('0')).arg(e.data.size()).arg(fpga::firmware_crc32(e.data),8,16,QChar('0'));QMetaObject::invokeMethod(this,[this,m]{log(m);},Qt::QueuedConnection);}});});
    connect(upgrade,&QPushButton::clicked,this,[this,upgrade]{
        const auto path=firmwarePath_->text();if(path.isEmpty()){log("请选择固件文件",true);return;}
        if(mode_->currentIndex()==0){log("UDP 图像模拟设备不包含 Flash 升级端口。固件解析可在模拟模式下运行。",true);return;}
        bool addressOk=false;uint32_t address=firmwareAddress_->text().toUInt(&addressOk,0);if(!addressOk){log("Flash 地址无效",true);return;}
        auto chip=static_cast<uint8_t>(firmwareTarget_->currentIndex());fpga::UpgradeOptions options;options.device_ip=deviceIp_->text().toStdString();options.local_ip=localIp_->text().toStdString();options.device_port=firmwarePort_->value();options.local_port=firmwarePort_->value();
        upgradeCancelled_=false;upgrade->setEnabled(false);firmwareProgress_->setValue(0);log("固件升级已提交 · "+path);
        enqueue([this,path,chip,address,options,upgrade]{try{
            std::vector<fpga::FirmwareEntry> entries;if(path.endsWith(".fpac",Qt::CaseInsensitive))entries=fpga::load_firmware_pack(path.toStdString());else entries={{chip,address,fpga::load_binary_file(path.toStdString())}};
            auto results=fpga::upgrade_firmware(std::move(entries),options,[this](const fpga::UpgradeProgress&p){QMetaObject::invokeMethod(this,[this,p]{firmwareProgress_->setValue(p.total?static_cast<int>(p.current*100/p.total):0);bottomStatus_->setText(QString("CHIP %1 · %2 · %3/%4").arg(p.chip_id).arg(QString::fromStdString(p.stage)).arg(p.current).arg(p.total));},Qt::QueuedConnection);},&upgradeCancelled_);
            QMetaObject::invokeMethod(this,[this,results,upgrade]{upgrade->setEnabled(true);for(auto&r:results)log(QString("CHIP %1 升级%2 %3").arg(r.chip_id).arg(r.success?"成功":"失败").arg(QString::fromStdString(r.error)),!r.success);},Qt::QueuedConnection);
        }catch(const std::exception&e){auto m=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,m,upgrade]{upgrade->setEnabled(true);log("固件升级失败: "+m,true);},Qt::QueuedConnection);}});
    });
    l->addWidget(fw);l->addStretch();return page;
}

QWidget* MainWindow::buildRegisters() {
    auto page=new QWidget;auto l=new QVBoxLayout(page);l->setContentsMargins(10,12,10,12);auto form=new QFormLayout;rawDevice_=spin(0,4,1);form->addRow("设备编号",rawDevice_);rawCommand_=new QLineEdit("0001");form->addRow("功能码 / HEX",rawCommand_);rawRw_=new QComboBox;rawRw_->addItems({"读取 · 0x80","写入 · 0x40"});form->addRow("读写标志",rawRw_);l->addLayout(form);l->addWidget(new QLabel("请求负载 / HEX"));rawPayload_=new QPlainTextEdit("00 00");rawPayload_->setMaximumHeight(120);l->addWidget(rawPayload_);auto send=button("发送原始寄存器命令",l,true);connect(send,&QPushButton::clicked,this,[this]{try{bool ok=false;uint16_t command=rawCommand_->text().toUShort(&ok,16);if(!ok)throw std::invalid_argument("功能码不是有效的 16 位 HEX");auto data=fromHex(rawPayload_->toPlainText());auto rw=rawRw_->currentIndex()?fpga::rw_write:fpga::rw_read;auto t=rawDevice_->value();submit("寄存器命令",[this,t,command,rw,data]{return device_.request(t,command,rw,data);},[this](const fpga::Response&r){rawResponse_->setPlainText(QString("设备 %1 · 功能码 0x%2 · MsgID %3\n\n%4\n\n完整命令帧\n%5").arg(r.frame.device).arg(r.frame.command,4,16,QChar('0')).arg(r.frame.message_id).arg(hex(r.frame.data)).arg(hex(fpga::encode_command(r.frame))));});}catch(const std::exception&e){log(QString::fromUtf8(e.what()),true);}});
    l->addWidget(new QLabel("应答数据与协议帧"));rawResponse_=new QPlainTextEdit;rawResponse_->setReadOnly(true);rawResponse_->setMinimumHeight(180);l->addWidget(rawResponse_,1);auto note=new QLabel("通过 SDK 校验帧头、帧长与 CRC，并按设备、功能码、消息 ID、读写标志匹配应答。");note->setObjectName("muted");note->setWordWrap(true);l->addWidget(note);return page;
}

QWidget* MainWindow::buildAnalysis() {
    focusDetector_=std::make_unique<fpga::FocusDetector>();
    auto page=new QWidget;auto layout=new QVBoxLayout(page);layout->setContentsMargins(10,5,10,12);
    auto focus=new QGroupBox("激光清晰度 / 对焦");auto fl=new QVBoxLayout(focus);focusLive_=new QCheckBox("实时计算当前相机（算法页打开时）");focusLive_->setChecked(true);fl->addWidget(focusLive_);focusWhole_=new QCheckBox("使用整幅图像");focusWhole_->setChecked(true);fl->addWidget(focusWhole_);
    auto roi=new QFormLayout;const QStringList names={"ROI X","ROI Y","ROI 宽","ROI 高"};for(int i=0;i<4;++i){focusRoi_[i]=spin(i<2?0:1,65535,i==2?512:i==3?256:0);roi->addRow(names[i],focusRoi_[i]);}fl->addLayout(roi);
    focusStatus_=new QLabel("等待图像 · 清晰度 / 峰值 / 灰度 / 饱和度");focusStatus_->setWordWrap(true);focusStatus_->setStyleSheet("color:#63d8c7;padding:8px;background:#142938;border-radius:5px;");fl->addWidget(focusStatus_);auto fr=new QHBoxLayout;auto calculate=button("计算当前帧",fr,true);auto peak=button("重置峰值",fr);fl->addLayout(fr);connect(calculate,&QPushButton::clicked,this,[this]{auto f=lastFrames_[selectedCamera()-1];if(f&&f->data.image)showFocus(*f->data.image);else log("请先采集图像",true);});connect(peak,&QPushButton::clicked,this,[this]{focusDetector_->reset_peak();});layout->addWidget(focus);
    auto native=new QGroupBox("原生算法 DLL / FPGA 对比");auto nl=new QVBoxLayout(native);
    auto fileField=[this,nl](const QString&label,const QString&path,const QString&filter){nl->addWidget(new QLabel(label));auto row=new QHBoxLayout;auto edit=new QLineEdit(path);row->addWidget(edit,1);auto select=button("选择",row);connect(select,&QPushButton::clicked,this,[this,edit,label,filter]{auto path=QFileDialog::getOpenFileName(this,label,{},filter);if(!path.isEmpty())edit->setText(path);});nl->addLayout(row);return edit;};
    nativeDll_=fileField("LaserProfiler DLL", "D:/CODE2/fpga_upper/fpga_sf_test/dll/laser_profiler_dll_v2.dll", "动态库 (*.dll)");nativeKernel_=fileField("kernel.xml", "D:/CODE2/fpga_upper/fpga_sf_test/data/kernel.xml", "XML (*.xml)");nativeNeural_=fileField("neural_net.xml", "D:/CODE2/fpga_upper/fpga_sf_test/data/neural_net.xml", "XML (*.xml)");auto nf=new QFormLayout;nativeThreshold_=spin(0,255,30);nf->addRow("灰度阈值",nativeThreshold_);nl->addLayout(nf);auto run=button("处理当前帧并对比 FPGA",nl,true);
    connect(run,&QPushButton::clicked,this,[this,run]{auto uploaded=lastFrames_[selectedCamera()-1];if(!uploaded||!uploaded->data.image){log("请先采集包含图像的相机数据块",true);return;}auto dll=nativeDll_->text().toStdString(),kernel=nativeKernel_->text().toStdString(),neural=nativeNeural_->text().toStdString();int threshold=nativeThreshold_->value();run->setEnabled(false);enqueue([this,run,uploaded,dll,kernel,neural,threshold]{try{fpga::NativeLaserProfiler profiler(dll);profiler.initialize(kernel,neural);if(uploaded->data.interp_count){fpga::NativeInterpolationConfig interpolation;interpolation.num_points=uploaded->data.interp_count;interpolation.x_interval=uploaded->data.interp_interval;interpolation.x_start=static_cast<float>(uploaded->data.interp_x_start);interpolation.x_end=static_cast<float>(uploaded->data.interp_x_end);profiler.set_interpolation_config(interpolation);}auto result=profiler.process(*uploaded->data.image,threshold,true);auto report=fpga::compare_sf(result,uploaded->data);auto describe=[](const QString&name,const fpga::ErrorStats&s){return QString("%1: %2\n有效比较 %3 · DLL %4 / FPGA %5\nMAE %6 · RMS %7 · P95 %8 · 最大 %9 %10\n").arg(name).arg(QString::fromStdString(s.status)).arg(s.count).arg(s.dll_count).arg(s.fpga_count).arg(s.mean_error,0,'g',6).arg(s.rms_error,0,'g',6).arg(s.p95_error,0,'g',6).arg(s.max_error,0,'g',6).arg(QString::fromStdString(s.unit));};QString summary=QString("原生处理耗时 %1 ms\n\n").arg(result.elapsed_ms,0,'f',2)+describe("中心线",report.center)+"\n"+describe("点云",report.pointcloud)+"\n"+describe("插值",report.interpolation);for(auto&note:report.notes)summary+="\n"+QString::fromStdString(note);QMetaObject::invokeMethod(this,[this,run,summary]{run->setEnabled(true);comparison_->setPlainText(summary);log("原生 DLL 处理 / FPGA 误差对比完成");},Qt::QueuedConnection);}catch(const std::exception&e){auto m=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,run,m]{run->setEnabled(true);comparison_->setPlainText(m);log("原生算法失败: "+m,true);},Qt::QueuedConnection);}});});
    comparison_=new QPlainTextEdit;comparison_->setReadOnly(true);comparison_->setMinimumHeight(230);nl->addWidget(comparison_);layout->addWidget(native);layout->addStretch();return page;
}

void MainWindow::showFocus(const fpga::Image& image) {
    try{std::optional<fpga::Roi> roi;if(!focusWhole_->isChecked())roi=fpga::Roi{focusRoi_[0]->value(),focusRoi_[1]->value(),focusRoi_[2]->value(),focusRoi_[3]->value()};auto result=focusDetector_->detect(image,roi);focusStatus_->setText(QString("清晰度 %1 · 历史峰值 %2\n平均灰度 %3 · 饱和度 %4%").arg(result.score,0,'f',3).arg(result.best_score,0,'f',3).arg(result.mean_gray,0,'f',2).arg(result.saturation,0,'f',2));}catch(const std::exception&e){focusStatus_->setText(QString::fromUtf8(e.what()));}
}

void MainWindow::enqueue(std::function<void()> job) { {std::lock_guard<std::mutex> lock(queueMutex_);if(workerStop_)return;jobs_.push_back(std::move(job));}queueCv_.notify_one(); }
void MainWindow::submit(const QString& label,std::function<fpga::Response()> operation,std::function<void(const fpga::Response&)> done) {
    if(!connected_){log("请先连接设备",true);return;}
    enqueue([this,label,operation=std::move(operation),done=std::move(done)]{try{auto response=operation();QMetaObject::invokeMethod(this,[this,label,response,done]{if(response){log(label+" ✓");try{if(done)done(response);}catch(const std::exception&e){log(QString::fromUtf8(e.what()),true);}}else log(label+"失败: "+QString::fromStdString(response.error),true);},Qt::QueuedConnection);}catch(const std::exception&e){QString m=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,label,m]{log(label+"失败: "+m,true);},Qt::QueuedConnection);}});
}
void MainWindow::connectDevice() {
    bool sim=mode_->currentIndex()==0;fpga::ConnectionOptions o;o.device_ip=deviceIp_->text().toStdString();o.device_port=devicePort_->value();o.local_ip=localIp_->text().toStdString();o.local_port=localPort_->value();connectButton_->setEnabled(false);
    enqueue([this,sim,o]()mutable{try{std::lock_guard<std::mutex> lock(lifecycleMutex_);if(sim){simulator_.start();o.device_ip="127.0.0.1";o.device_port=simulator_.port();o.local_ip="127.0.0.1";o.local_port=0;}device_.open(o);auto probe=device_.read(0,0x401,{std::chrono::milliseconds{1000},0});if(!probe){device_.close();throw std::runtime_error("设备心跳未确认: "+probe.error);}QMetaObject::invokeMethod(this,[this,sim,o]{setConnected(true);log(QString("%1 · %2:%3 → 本地 :%4").arg(sim?"UDP 模拟设备已连接":"FPGA 已连接").arg(QString::fromStdString(o.device_ip)).arg(o.device_port).arg(device_.local_port()));},Qt::QueuedConnection);}catch(const std::exception&e){auto m=QString::fromUtf8(e.what());simulator_.stop();QMetaObject::invokeMethod(this,[this,m]{setConnected(false);log("连接失败: "+m,true);},Qt::QueuedConnection);}});
}
void MainWindow::disconnectDevice() {connectButton_->setEnabled(false);captureButton_->setChecked(false);capturing_=false;enqueue([this]{std::lock_guard<std::mutex> lock(lifecycleMutex_);device_.close();simulator_.stop();QMetaObject::invokeMethod(this,[this]{setConnected(false);log("设备已断开");},Qt::QueuedConnection);});}
void MainWindow::setConnected(bool value) {
    connected_=value;connectButton_->setEnabled(true);connectButton_->setText(value?"断开连接":"连接设备");mode_->setEnabled(!value);deviceIp_->setEnabled(!value);devicePort_->setEnabled(!value);localIp_->setEnabled(!value);localPort_->setEnabled(!value);connectionStatus_->setText(value?"●  设备在线":"●  未连接");connectionStatus_->setStyleSheet(value?"color:#55dfc4;":"color:#94a5ba;");bottomStatus_->setText(value?"设备在线 · 参数写入后等待应答确认":"准备就绪");captureButton_->setText("启动采集");
    if(value){lastHeartbeatMs_=QDateTime::currentMSecsSinceEpoch();confirmedParameters_.clear();frameCounts_={};lastFrames_={};for(auto frame:frames_)frame->setFrame({},0);for(int row=0;row<parameters_->rowCount();++row)updateConfirmedParameter(parameters_->item(row,0)->data(Qt::UserRole).toUInt());}
}
std::vector<uint16_t> MainWindow::cameraTargets()const {int target=target_->currentIndex();if(target>=1&&target<=4)return{static_cast<uint16_t>(target)};return{1,2,3,4};}
uint16_t MainWindow::selectedCamera()const {int target=target_->currentIndex();return static_cast<uint16_t>(target>=1&&target<=4?target:1);}
void MainWindow::capture(bool enabled) {if(!connected_){captureButton_->setChecked(false);log("请先连接设备",true);return;}capturing_=enabled;imageRequest_->setEnabled(!enabled);captureInterval_->setEnabled(!enabled);captureButton_->setText(enabled?"停止采集":"启动采集");uint8_t mask=0;for(auto t:cameraTargets())mask|=1<<(t-1);bool rearranged=imageRequest_->currentIndex()==0;int interval=captureInterval_->value();enqueue([this,enabled,mask,rearranged,interval]{if(enabled)device_.start_capture(mask,rearranged,std::chrono::milliseconds{interval});else device_.stop_capture();});log(enabled?"已启动四路图像轮询 · 每路单请求在途":"图像采集已停止");}
void MainWindow::singleCapture() {auto targets=cameraTargets();bool rearranged=imageRequest_->currentIndex()==0;submit("单帧采集",[this,targets,rearranged]{fpga::Response r;for(auto t:targets){r=device_.get_camera_data(t,rearranged);if(!r)return r;}return r;});}
void MainWindow::rebuildParameters() {
    parameters_->setRowCount(0);bool fpgaTarget=target_->currentIndex()==5;
    for(const auto& spec:fpga::command_specs()){
        bool global=spec.device_target==fpga::DeviceTarget::Fpga;if(global!=fpgaTarget)continue;
        if(spec.code>=0x300&&spec.code<=0x401)continue;if(spec.code==0x203||spec.code==0x204||spec.code==0x205||spec.code==0x206)continue;
        if(spec.code==0x501||spec.code==0x505)continue;int row=parameters_->rowCount();parameters_->insertRow(row);
        auto name=new QTableWidgetItem(chineseName(spec.code));name->setData(Qt::UserRole,spec.code);name->setToolTip(QString("0x%1 · 写入 %2B · 读取 %3B").arg(spec.code,4,16,QChar('0')).arg(spec.write_size).arg(spec.read_size));name->setFlags(name->flags()&~Qt::ItemIsEditable);parameters_->setItem(row,0,name);
        auto edit=new QTableWidgetItem(defaultValue(spec.code));if(spec.read_only)edit->setFlags(edit->flags()&~Qt::ItemIsEditable);parameters_->setItem(row,1,edit);auto confirmed=new QTableWidgetItem("—");confirmed->setFlags(confirmed->flags()&~Qt::ItemIsEditable);parameters_->setItem(row,2,confirmed);parameters_->setRowHeight(row,36);
    }if(parameters_->rowCount())parameters_->selectRow(0);
    for(int row=0;row<parameters_->rowCount();++row)updateConfirmedParameter(parameters_->item(row,0)->data(Qt::UserRole).toUInt());
}

void MainWindow::updateConfirmedParameter(uint16_t command) {
    auto targets=target_->currentIndex()==5?std::vector<uint16_t>{0}:cameraTargets();QStringList detail;QString common;bool different=false;bool first=true;
    for(auto t:targets){auto it=confirmedParameters_.find({t,command});if(it==confirmedParameters_.end()){detail<<QString("CAM %1: 未回读").arg(t);if(!first)different=true;continue;}auto text=decodeParameter(command,it->second);detail<<QString("%1 %2: %3").arg(t?"CAM":"FPGA").arg(t).arg(text);if(first){common=text;first=false;}else if(common!=text)different=true;}
    for(int row=0;row<parameters_->rowCount();++row)if(parameters_->item(row,0)->data(Qt::UserRole).toUInt()==command){auto item=parameters_->item(row,2);item->setText(first?"—":different?"各路不同":common);item->setToolTip(detail.join('\n'));item->setForeground(QColor(first?"#8597ad":"#62dac7"));}
}

fpga::Bytes MainWindow::encodeParameter(uint16_t command,const QString& text)const {
    if(command==0x504||command==0x506)return fpga::pack_model(text.toStdString());
    auto fields=text.split(',',Qt::SkipEmptyParts);std::vector<int64_t> ints;
    if(command==10)return fpga::pack_u32(fpga::sample_rate_to_ticks(text.toDouble()));
    if(command==0x207){if(fields.size()!=4)throw std::invalid_argument("插值配置为起点mm, 上界mm, 间隔mm, 点数");return fpga::pack_interpolation_config(fpga::InterpolationConfig::from_mm(fields[0].toDouble(),fields[1].toDouble(),fields[2].toDouble(),fields[3].toUInt()));}
    for(auto f:fields){bool ok=false;auto v=f.trimmed().toLongLong(&ok,0);if(!ok||v<0||v>UINT32_MAX)throw std::invalid_argument("参数应为 0 至 4294967295 的整数");ints.push_back(v);}
    if(command==3||command==4){if(ints.size()!=2||ints[0]>65535||ints[1]>65535)throw std::invalid_argument("ROI 需要两个 uint16 字段");return fpga::pack_roi(ints[0],ints[1]);}
    if(command==5){if(ints.size()!=4)throw std::invalid_argument("CMOS 区域需要四个字段");return fpga::pack_cmos_area({static_cast<int32_t>(ints[0]),static_cast<int32_t>(ints[1]),static_cast<int32_t>(ints[2]),static_cast<int32_t>(ints[3])});}
    if(command==6){if(ints.size()!=4)throw std::invalid_argument("曝光延迟需要四路计数");return fpga::pack_exposure_delays({uint32_t(ints[0]),uint32_t(ints[1]),uint32_t(ints[2]),uint32_t(ints[3])});}
    if(command==16){if(ints.size()!=2)throw std::invalid_argument("激光延迟需要提前与延迟两个计数");return fpga::pack_laser_delay(ints[0],ints[1]);}
    if(command==18||command==19){if(ints.size()!=2)throw std::invalid_argument("HDR 参数需要两个 uint32 字段");auto data=fpga::pack_u32(static_cast<uint32_t>(ints[0]));auto second=fpga::pack_u32(static_cast<uint32_t>(ints[1]));data.insert(data.end(),second.begin(),second.end());return data;}
    if(ints.size()!=1)throw std::invalid_argument("此参数需要一个整数");return fpga::pack_u32(ints[0]);
}
QString MainWindow::decodeParameter(uint16_t command,const fpga::Bytes& data)const {
    if(command==7||command==0x502||command==0x504||command==0x506)return QString::fromUtf8(reinterpret_cast<const char*>(data.data()),data.size()).remove(QChar('\0'));
    if(command==10)return QString::number(fpga::sample_ticks_to_rate(fpga::read_u32(data)),'g',7)+" Hz";
    if(command==3||command==4){uint32_t v=fpga::read_u32(data);return QString("%1, %2").arg(v>>16).arg(v&0xffff);}
    if(command==0x207){auto c=fpga::unpack_interpolation_config(data);return QString("%1,%2,%3,%4").arg(c.start_mm()).arg(c.end_mm()).arg(c.interval_mm()).arg(c.count);}
    if(data.size()%4==0){QStringList fields;for(size_t i=0;i<data.size();i+=4)fields<<QString::number(fpga::read_u32(data,i));return fields.join(", ");}return hex(data);
}
void MainWindow::readParameters(bool all) {
    if(!connected_){log("请先连接设备",true);return;}std::vector<uint16_t> commands;
    for(int row=0;row<parameters_->rowCount();++row)if(all||row==parameters_->currentRow()){auto cmd=static_cast<uint16_t>(parameters_->item(row,0)->data(Qt::UserRole).toUInt());auto spec=fpga::command_spec(cmd);if(spec&&spec->write_only){if(!all)log("此参数仅支持写入，不能回读",true);continue;}commands.push_back(cmd);}
    auto targets=target_->currentIndex()==5?std::vector<uint16_t>{0}:cameraTargets();
    enqueue([this,commands,targets]{for(auto t:targets)for(auto c:commands){auto r=device_.read(t,c);QMetaObject::invokeMethod(this,[this,t,c,r]{if(!r){log(QString("设备 %1 · %2 回读失败: %3").arg(t).arg(chineseName(c)).arg(QString::fromStdString(r.error)),true);return;}try{auto text=decodeParameter(c,r.frame.data);confirmedParameters_[{t,c}]=r.frame.data;updateConfirmedParameter(c);log(QString("设备 %1 · %2 = %3").arg(t).arg(chineseName(c)).arg(text));}catch(const std::exception&e){log(QString("设备 %1 · %2 无效应答: %3").arg(t).arg(chineseName(c)).arg(QString::fromUtf8(e.what())),true);}},Qt::QueuedConnection);}});
}
void MainWindow::writeParameter() {
    int row=parameters_->currentRow();if(row<0)return;auto command=static_cast<uint16_t>(parameters_->item(row,0)->data(Qt::UserRole).toUInt());auto spec=fpga::command_spec(command);if(spec&&spec->read_only){log("该参数只读",true);return;}
    try{auto data=encodeParameter(command,parameters_->item(row,1)->text());auto targets=target_->currentIndex()==5?std::vector<uint16_t>{0}:cameraTargets();bool writeOnly=spec&&spec->write_only;
        submit(chineseName(command)+(writeOnly?"写入":"写入 / 回读"),[this,command,data,targets,writeOnly]{fpga::Response r;for(auto t:targets){r=device_.write(t,command,data);if(!r)return r;if(!writeOnly){r=device_.read(t,command);if(!r)return r;if(r.frame.data!=data)throw std::runtime_error("参数回读与写入不一致");}}return r;},[this,command,targets,data,writeOnly](const fpga::Response&){for(auto t:targets)confirmedParameters_[{t,command}]=data;updateConfirmedParameter(command);if(writeOnly)log("参数写入应答成功 · 设备不支持此参数回读");});
    }catch(const std::exception&e){log(QString::fromUtf8(e.what()),true);}
}

void MainWindow::updateInterpolationPreview() {try{auto c=fpga::InterpolationConfig::from_mm(interpStart_->value(),interpEnd_->value(),interpInterval_->value(),interpCount_->value());interpPreview_->setText(QString("%1 点 · 首点 %2 mm\n实际末点 %3 mm\n量化间隔 %4 mm\nQ16.16: %5 / %6 / %7").arg(c.count).arg(c.start_mm(),0,'g',8).arg(c.last_mm(),0,'g',8).arg(c.interval_mm(),0,'f',8).arg(c.x_start).arg(c.x_end).arg(c.interval));}catch(const std::exception&e){interpPreview_->setText(QString::fromUtf8(e.what()));}}
void MainWindow::updateStatistics() {
    auto s=device_.statistics();frameMetric_->setText(QString("%1 帧").arg(s.decoded_images));trafficMetric_->setText(QString("%1 MB").arg(s.rx_bytes/1048576.0,0,'f',2));integrityMetric_->setText(QString("%1 异常  /  %2 重试").arg(s.malformed_packets+s.incomplete_frames).arg(s.retries));
    if(connected_){if(++heartbeatTick_%2==0)device_.heartbeat();bool fresh=QDateTime::currentMSecsSinceEpoch()-lastHeartbeatMs_<3500;if(!fresh){connectionStatus_->setText("●  心跳超时");connectionStatus_->setStyleSheet("color:#f2ae68;");}bottomStatus_->setText(QString("%1 · 接收 %2 包 · 完整帧 %3 · 在途请求 %4 · 队列 %5").arg(fresh?"在线":"心跳超时").arg(s.rx_packets).arg(s.assembled_frames).arg(s.pending_requests).arg(s.queued_events));}
}
void MainWindow::log(const QString& message,bool error) {logs_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")+ (error?"  [ERROR]  ":"  [INFO]   ")+message);if(smoke_&&error)smokeFailed_=true;}

void MainWindow::exportCurrentData() {
    auto f=lastFrames_[selectedCamera()-1];if(!f){log("当前没有可导出的数据",true);return;}auto path=QFileDialog::getSaveFileName(this,"导出图像 / 数值数据",QString("cam%1.png").arg(selectedCamera()),"PNG 图像 (*.png);;数值数据 CSV (*.csv)");if(path.isEmpty())return;
    if(path.endsWith(".csv",Qt::CaseInsensitive)){
        QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){log("无法写入数据文件",true);return;}QByteArray csv;
        if(plotMode_->currentIndex()==3){csv="frame,timestamp,batch_index,point,x_mm,z_mm\n";for(const auto&r:lastBatch_.records)for(size_t i=0;i<r.heights.size();++i){double x=static_cast<int32_t>(lastBatch_.x_start_raw)/65536.0+i*static_cast<int32_t>(lastBatch_.interval_raw)/65536.0;csv+=QString("%1,%2,%3,%4,%5,%6\n").arg(r.frame_no).arg(r.timestamp).arg(r.batch_index).arg(i).arg(x,0,'g',12).arg(r.heights[i],0,'g',12).toUtf8();}}
        else if(plotMode_->currentIndex()==1){csv="x_mm,y_mm,z_mm\n";for(const auto&p:f->data.points3d)csv+=QString("%1,%2,%3\n").arg(p.x,0,'g',12).arg(p.y,0,'g',12).arg(p.z,0,'g',12).toUtf8();}
        else {csv="x,z\n";const auto&points=plotMode_->currentIndex()==2?f->data.interpolated:f->data.centerline;for(const auto&p:points)csv+=QString("%1,%2\n").arg(p.x,0,'g',12).arg(p.z,0,'g',12).toUtf8();}
        file.write(csv);log("数值数据已导出: "+path);
    }else if(!frames_[selectedCamera()-1]->image().save(path))log("图像保存失败",true);else log("图像已保存: "+path);
}

void MainWindow::receiveCamera(std::shared_ptr<const fpga::CameraEvent> event) {
    if(!event||event->device<1||event->device>4)return;auto index=event->device-1;
    if(event->data.image&&lastFrames_[index]!=event){const auto&image=*event->data.image;
        QImage qtImage(image.pixels.data(),static_cast<int>(image.width),static_cast<int>(image.height),static_cast<int>(image.width),QImage::Format_Grayscale8);
        frames_[index]->setFrame(qtImage.copy(),event->sequence);++frameCounts_[index];}
    if(event->data.image||!lastFrames_[index])lastFrames_[index]=event;
    if(event->data.image&&event->device==selectedCamera()&&controls_->currentIndex()==5&&focusLive_->isChecked())showFocus(*event->data.image);
    if(event->device!=selectedCamera()||plotMode_->currentIndex()==3)return;
    std::vector<QPointF> points;QString caption;
    switch(plotMode_->currentIndex()){
    case 1:for(const auto&p:event->data.points3d)if(fpga::valid_numeric(p.x)&&fpga::valid_numeric(p.z))points.emplace_back(p.x,p.z);caption="3D 点云 · X-Z 投影 / mm";break;
    case 2:for(const auto&p:event->data.interpolated)if(fpga::valid_numeric(p.x)&&fpga::valid_numeric(p.z))points.emplace_back(p.x,p.z);caption="3D 插值网格 / mm";break;
    default:for(const auto&p:event->data.centerline)if(fpga::valid_numeric(p.x)&&fpga::valid_numeric(p.z))points.emplace_back(p.x,p.z);if(points.empty())for(const auto&p:event->data.contour)if(fpga::valid_numeric(p.x)&&fpga::valid_numeric(p.z))points.emplace_back(p.x,p.z);caption="中心线 / 轮廓 · X-Z / pixel";break;
    }
    if(!points.empty())profile_->setPoints(std::move(points),QString("CAM %1 · %2").arg(event->device).arg(caption));
}
void MainWindow::receiveBatch(const fpga::BatchEvent& event) {
    batchCount_+=event.data.records.size();lastBatch_=event.data;batchStatus_->setText(QString("● 已接收 %1 条批记录 · 当前 %2 点").arg(batchCount_).arg(event.data.point_count));
    if(event.data.records.empty())return;const auto&r=event.data.records.back();std::vector<QPointF> points;points.reserve(r.heights.size());
    for(size_t i=0;i<r.heights.size();++i){double x=static_cast<int32_t>(event.data.x_start_raw)/65536.0+i*static_cast<int32_t>(event.data.interval_raw)/65536.0;if(fpga::valid_numeric(r.heights[i]))points.emplace_back(x,r.heights[i]);}
    plotMode_->setCurrentIndex(3);profile_->setPoints(std::move(points),QString("CAM %1 · 批 %2 · 帧 %3 · %4 点 / mm").arg(event.device).arg(r.batch_index).arg(r.frame_no).arg(r.heights.size()));
    log(QString("批数据 · CAM %1 · %2 记录 × %3 点").arg(event.device).arg(event.data.records.size()).arg(event.data.point_count));
}

void MainWindow::startSimulation() {
    mode_->setCurrentIndex(0);connectDevice();auto timer=new QTimer(this);timer->setInterval(100);int*attempts=new int(0);
    connect(timer,&QTimer::timeout,this,[this,timer,attempts]{if(connected_){timer->stop();timer->deleteLater();delete attempts;captureButton_->setChecked(true);capture(true);readParameters(true);}else if(++*attempts>=100){timer->stop();timer->deleteLater();delete attempts;}});timer->start();
}

void MainWindow::runSmokeTest(const QString& screenshotPath) {
    smoke_=true;smokePath_=screenshotPath;mode_->setCurrentIndex(0);connectDevice();
    auto timer=new QTimer(this);timer->setInterval(100);int*attempts=new int(0);
    connect(timer,&QTimer::timeout,this,[this,timer,attempts]{
        if(!connected_){if(++*attempts<80)return;timer->stop();timer->deleteLater();delete attempts;log("SMOKE: 连接超时",true);QApplication::exit(2);return;}
        timer->stop();timer->deleteLater();delete attempts;captureButton_->setChecked(true);capture(true);
        parameters_->item(0,1)->setText("6500");writeParameter();
        enqueue([this]{try{
            auto require=[](const fpga::Response&r){if(!r)throw std::runtime_error(r.error);};
            for(uint16_t t=1;t<=4;++t){auto r=device_.read(t,1);require(r);if(fpga::read_u32(r.frame.data)!=6500)throw std::runtime_error("曝光回读不一致");auto frame=device_.wait_next_frame(t,0,std::chrono::milliseconds{3000});if(!frame||!frame->data.image)throw std::runtime_error("相机没有收到真实 UDP 图像");fpga::FocusDetector detector;if(detector.detect(*frame->data.image).score<=0)throw std::runtime_error("清晰度计算异常");}
            require(device_.write_u32(0,0x101,3));require(device_.write_u32(0,0x102,1));require(device_.write_u32(0,0x103,1));require(device_.set_sample_rate(1500));require(device_.set_exposure_delay({0,50,100,150}));
            auto knees=fpga::pack_u32(50);auto secondKnee=fpga::pack_u32(100);knees.insert(knees.end(),secondKnee.begin(),secondKnee.end());require(device_.write(1,0x12,knees));auto hdr=device_.read(1,0x12);require(hdr);if(hdr.frame.data!=knees)throw std::runtime_error("HDR 回读不一致");
            require(device_.set_batch_length(50));require(device_.set_batch_control(true,0));require(device_.get_batch_data(1,false));require(device_.get_batch_data(1,true));require(device_.set_batch_control(false));
            auto c=fpga::InterpolationConfig::from_mm(-10,10,.1,200);auto payload=fpga::pack_interpolation_config(c);require(device_.write(1,0x207,payload));auto r=device_.read(1,0x207);require(r);if(r.frame.data!=payload)throw std::runtime_error("插值配置回读不一致");
            auto raw=device_.request(1,2,fpga::rw_read,{0,0});require(raw);require(device_.reset_buffer(1));
            QMetaObject::invokeMethod(this,[this,raw]{rawResponse_->setPlainText(hex(raw.frame.data));log("SMOKE PASS · UI 参数写入回读 / 四路 UDP 图像 / HDR / 对焦 / 触发 / 批数据 / 插值 / 原始寄存器 / 缓存重置");
                QTimer::singleShot(900,this,[this]{bool complete=batchCount_>0;for(auto n:frameCounts_)complete=complete&&n>0;if(!complete)log("SMOKE: 图像或批回调未完成",true);updateStatistics();controls_->setCurrentIndex(0);plotMode_->setCurrentIndex(0);auto f=lastFrames_[0];if(f)receiveCamera(f);auto image=grab();if(!image.save(smokePath_))log("SMOKE: 截图保存失败",true);QFile logfile(smokePath_+".log");if(logfile.open(QIODevice::WriteOnly|QIODevice::Text))logfile.write(logs_->toPlainText().toUtf8());QApplication::exit(smokeFailed_?2:0);});
            },Qt::QueuedConnection);
        }catch(const std::exception&e){auto m=QString::fromUtf8(e.what());QMetaObject::invokeMethod(this,[this,m]{log("SMOKE FAIL · "+m,true);grab().save(smokePath_);QFile logfile(smokePath_+".log");if(logfile.open(QIODevice::WriteOnly|QIODevice::Text))logfile.write(logs_->toPlainText().toUtf8());QApplication::exit(2);},Qt::QueuedConnection);}});
    });timer->start();QTimer::singleShot(20000,this,[this]{log("SMOKE: 总测试超时",true);QApplication::exit(2);});
}




