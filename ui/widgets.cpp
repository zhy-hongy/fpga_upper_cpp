#include "widgets.h"
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFontMetrics>
#include <QDateTime>
#include <QImage>
#include <QHBoxLayout>
#include <cmath>
#include <algorithm>
#include <opencv2/imgproc.hpp>

#define CLAMP(val, lo, hi) std::max<double>((lo), std::min<double>((hi), (val)))

// ══════════════════════════════════════════════════
// ImageView
// ══════════════════════════════════════════════════

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setMinimumSize(400, 300);
    setStyleSheet("background-color: #0D0D0D; border: 1px solid #3A3A3A;");
    setRenderHint(QPainter::SmoothPixmapTransform);
    setRenderHint(QPainter::Antialiasing);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);

    m_centerline_item = new QGraphicsPathItem();
    m_centerline_item->setZValue(10);
    m_centerline_item->setVisible(false);
    QPen cpen(QColor("#00FF88"), 2.0);
    cpen.setCosmetic(true);
    m_centerline_item->setPen(cpen);
    m_scene->addItem(m_centerline_item);

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { viewport()->update(); });
}

void ImageView::set_calibration(double x_res, double z_res, double center_x, double ref_y) {
    m_x_resolution = x_res;
    m_z_resolution = z_res;
    if (center_x >= 0) m_center_x = center_x;
    if (ref_y >= 0) m_reference_y = ref_y;
    viewport()->update();
}

double ImageView::pixel_to_physical_x(double px) const {
    return (px - m_center_x) * m_x_resolution;
}

double ImageView::pixel_to_physical_z(double py) const {
    return (m_reference_y - py) * m_z_resolution;
}

void ImageView::set_image(const cv::Mat& arr) {
    if (arr.empty()) return;

    bool size_changed = (arr.cols != m_image_w || arr.rows != m_image_h);
    m_image_w = arr.cols;
    m_image_h = arr.rows;

    if (!m_user_zoomed || size_changed) {
        m_center_x = m_image_w / 2.0;
        m_reference_y = m_image_h / 2.0;
    }

    // Convert cv::Mat to QImage
    QImage qimg;
    if (arr.type() == CV_8UC1) {
        qimg = QImage(arr.data, arr.cols, arr.rows, arr.cols, QImage::Format_Grayscale8);
    } else if (arr.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(arr, rgb, cv::COLOR_BGR2RGB);
        qimg = QImage(rgb.data, rgb.cols, rgb.rows, rgb.cols * 3, QImage::Format_RGB888).copy();
    } else {
        cv::Mat gray;
        if (arr.channels() == 3) {
            cv::cvtColor(arr, gray, cv::COLOR_BGR2GRAY);
        } else if (arr.channels() == 4) {
            cv::cvtColor(arr, gray, cv::COLOR_BGRA2GRAY);
        } else {
            gray = arr.clone();
        }
        qimg = QImage(gray.data, gray.cols, gray.rows, gray.cols, QImage::Format_Grayscale8).copy();
    }

    m_pixmap = QPixmap::fromImage(qimg);

    if (!m_image_item) {
        m_image_item = new QGraphicsPixmapItem();
        m_image_item->setZValue(0);
        m_scene->addItem(m_image_item);
    }

    m_image_item->setPixmap(m_pixmap);
    m_image_item->setVisible(m_show_image);
    m_scene->setSceneRect(QRectF(0, 0, m_image_w, m_image_h));

    if (!m_user_zoomed || size_changed) {
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        m_zoom_factor = 1.0;
        m_user_zoomed = false;
    }

    emit frame_changed(arr);
}

void ImageView::clear_image() {
    m_pixmap = QPixmap();
    if (m_image_item) {
        m_image_item->setPixmap(QPixmap());
        m_image_item->setVisible(false);
    }
    m_scene->setSceneRect(QRectF(0, 0, 400, 300));
}

void ImageView::set_camera_frame(const CameraFrame& frame) {
    if (frame.has_image && !frame.image.empty())
        set_image(frame.image);
    if (frame.has_profile) {
        if (!frame.profile.isEmpty())
            set_contour_data(frame.profile);
        if (!frame.profile_image_points.isEmpty())
            set_contour_image_points(frame.profile_image_points);
    }
}

void ImageView::set_camera_label(const QString& label) {
    m_camera_label = label;
    viewport()->update();
}

void ImageView::set_show_image(bool enabled) {
    m_show_image = enabled;
    if (m_image_item) {
        m_image_item->setVisible(enabled);
    }
    if (enabled && !m_pixmap.isNull()) {
        m_scene->setSceneRect(QRectF(0, 0, m_image_w, m_image_h));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (!m_user_zoomed) {
            fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
            m_zoom_factor = 1.0;
        }
    }
    update_centerline_visibility();
}

void ImageView::set_show_contour(bool enabled) {
    m_show_contour = enabled;
    if (!enabled) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    update_centerline_visibility();
    viewport()->update();
}

void ImageView::set_show_centerline(bool enabled) {
    m_show_centerline = enabled;
    update_centerline_visibility();
    viewport()->update();
}

void ImageView::set_show_3d(bool enabled) {
    m_show_3d = enabled;
    if (enabled) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    viewport()->update();
}

void ImageView::set_contour_data(const QVector<QPair<float, float>>& points) {
    if (points.isEmpty()) {
        m_contour_points.clear();
        m_contour_range_valid = false;
        return;
    }
    m_contour_points = points;

    float x_min = points[0].first, x_max = points[0].first;
    float z_min = points[0].second, z_max = points[0].second;
    for (const auto& p : points) {
        x_min = std::min(x_min, p.first);
        x_max = std::max(x_max, p.first);
        z_min = std::min(z_min, p.second);
        z_max = std::max(z_max, p.second);
    }
    double x_pad = std::max((x_max - x_min) * 0.05, 0.5);
    double z_pad = std::max((z_max - z_min) * 0.05, 0.5);
    m_contour_x_range = {x_min - x_pad, x_max + x_pad};
    m_contour_z_range = {z_min - z_pad, z_max + z_pad};
    m_contour_zoom = 1.0;
    m_contour_pan_x = 0.0;
    m_contour_pan_z = 0.0;
    m_contour_range_valid = true;
    viewport()->update();
}

void ImageView::set_contour_image_points(const QVector<QPair<float, float>>& points) {
    m_contour_image_points = points;
    if (points.isEmpty()) {
        m_centerline_item->setPath(QPainterPath());
        m_centerline_item->setVisible(false);
    } else {
        QPainterPath path;
        path.moveTo(points[0].first, points[0].second);
        for (int i = 1; i < points.size(); i++)
            path.lineTo(points[i].first, points[i].second);
        m_centerline_item->setPath(path);
        m_centerline_item->setVisible(m_show_centerline && m_show_image);
    }
    viewport()->update();
}

void ImageView::update_centerline_visibility() {
    if (m_centerline_item) {
        m_centerline_item->setVisible(m_show_centerline && m_show_image && !m_contour_image_points.isEmpty());
    }
}

void ImageView::clear_all() {
    clear_image();
    m_contour_points.clear();
    m_contour_range_valid = false;
    m_contour_image_points.clear();
    if (m_centerline_item) {
        m_centerline_item->setPath(QPainterPath());
        m_centerline_item->setVisible(false);
    }
    reset_batch();
    viewport()->update();
}

// ── Measure lines ──

void ImageView::set_measure_mode(const QString& mode) {
    m_measure_mode = mode;
    if (mode == "vertical") {
        QWidget* vp = viewport();
        int gl = m_margin_left, gt = m_margin_top;
        int gr = vp->width() - m_margin_right, gb = vp->height() - m_margin_bottom;
        m_measure_vlines_visible = {true, true};
        m_measure_hlines_visible = {false, false};
        m_measure_vlines_mm[0] = px_to_mm_x(gl + (gr - gl) * 0.35, gl, gt, gr, gb);
        m_measure_vlines_mm[1] = px_to_mm_x(gl + (gr - gl) * 0.65, gl, gt, gr, gb);
        sync_measure_pixels();
    } else if (mode == "horizontal") {
        QWidget* vp = viewport();
        int gl = m_margin_left, gt = m_margin_top;
        int gr = vp->width() - m_margin_right, gb = vp->height() - m_margin_bottom;
        m_measure_vlines_visible = {false, false};
        m_measure_hlines_visible = {true, true};
        m_measure_hlines_mm[0] = px_to_mm_z(gt + (gb - gt) * 0.35, gl, gt, gr, gb);
        m_measure_hlines_mm[1] = px_to_mm_z(gt + (gb - gt) * 0.65, gl, gt, gr, gb);
        sync_measure_pixels();
    } else {
        m_measure_vlines_visible = {false, false};
        m_measure_hlines_visible = {false, false};
    }
    viewport()->update();
}

void ImageView::set_measure_line_visible(const QString& mode, int index, bool visible) {
    QWidget* vp = viewport();
    int gl = m_margin_left, gt = m_margin_top;
    int gr = vp->width() - m_margin_right, gb = vp->height() - m_margin_bottom;

    if (mode == "vertical") {
        m_measure_vlines_visible[index] = visible;
        if (visible) {
            double ratio = (index == 0) ? 0.35 : 0.65;
            m_measure_vlines_mm[index] = px_to_mm_x(gl + (gr - gl) * ratio, gl, gt, gr, gb);
        }
    } else {
        m_measure_hlines_visible[index] = visible;
        if (visible) {
            double ratio = (index == 0) ? 0.35 : 0.65;
            m_measure_hlines_mm[index] = px_to_mm_z(gt + (gb - gt) * ratio, gl, gt, gr, gb);
        }
    }
    sync_measure_pixels();

    bool any_v = m_measure_vlines_visible[0] || m_measure_vlines_visible[1];
    bool any_h = m_measure_hlines_visible[0] || m_measure_hlines_visible[1];
    if (any_v) m_measure_mode = "vertical";
    else if (any_h) m_measure_mode = "horizontal";
    else m_measure_mode.clear();
    viewport()->update();
}

// ── Batch / 3D ──

void ImageView::start_batch() {
    m_batch_lines.clear();
    m_batch_line_index = 0;
    m_batch_running = true;
    m_batch_range_valid = false;
    viewport()->update();
}

void ImageView::set_batch_total_count(int count) {
    m_batch_total_count = count;
    viewport()->update();
}

void ImageView::stop_batch() {
    m_batch_running = false;
    viewport()->update();
}

void ImageView::reset_batch() {
    m_batch_lines.clear();
    m_batch_line_index = 0;
    m_batch_range_valid = false;
    viewport()->update();
}

void ImageView::add_batch_line(const QVector<QPair<float, float>>& points) {
    if (points.isEmpty()) return;

    QVector<QPair<float, float>> valid;
    for (const auto& p : points) {
        if (p.first != -999.0f && p.second != -999.0f)
            valid.append(p);
    }
    if (valid.isEmpty()) return;

    m_batch_lines.append({m_batch_line_index, valid});

    float x_min = valid[0].first, x_max = valid[0].first;
    float z_min = valid[0].second, z_max = valid[0].second;
    for (const auto& p : valid) {
        x_min = std::min(x_min, p.first);
        x_max = std::max(x_max, p.first);
        z_min = std::min(z_min, p.second);
        z_max = std::max(z_max, p.second);
    }

    if (!m_batch_range_valid) {
        m_batch_x_range = {x_min, x_max};
        m_batch_z_range = {z_min, z_max};
        m_batch_range_valid = true;
    } else {
        m_batch_x_range.first = std::min(m_batch_x_range.first, x_min);
        m_batch_x_range.second = std::max(m_batch_x_range.second, x_max);
        m_batch_z_range.first = std::min(m_batch_z_range.first, z_min);
        m_batch_z_range.second = std::max(m_batch_z_range.second, z_max);
    }

    m_batch_line_index++;
    if (m_batch_total_count > 0 && m_batch_line_index >= m_batch_total_count) {
        m_batch_running = false;
        emit batch_complete();
    }
    viewport()->update();
}

int ImageView::get_3d_total_lines() const {
    int actual = m_batch_lines.size();
    int base = std::max({actual, m_batch_total_count, 600});
    if (base > 600) {
        int step = 100;
        base = ((base + step - 1) / step) * step;
    }
    return base;
}

// ── Zoom ──

void ImageView::zoom_row_in() {
    m_zoom_y = std::min(m_zoom_y * 1.15, 60.0);
    m_user_zoomed = true;
    if (!(m_show_contour && !m_contour_points.isEmpty() && !m_show_image))
        apply_image_zoom(1.0, 1.15);
    viewport()->update();
}

void ImageView::zoom_row_out() {
    m_zoom_y = std::max(m_zoom_y / 1.15, 1.0);
    m_user_zoomed = true;
    if (!(m_show_contour && !m_contour_points.isEmpty() && !m_show_image))
        apply_image_zoom(1.0, 1.0 / 1.15);
    viewport()->update();
}

void ImageView::zoom_col_in() {
    m_zoom_x = std::min(m_zoom_x * 1.15, 60.0);
    m_user_zoomed = true;
    if (!(m_show_contour && !m_contour_points.isEmpty() && !m_show_image))
        apply_image_zoom(1.15, 1.0);
    viewport()->update();
}

void ImageView::zoom_col_out() {
    m_zoom_x = std::max(m_zoom_x / 1.15, 1.0);
    m_user_zoomed = true;
    if (!(m_show_contour && !m_contour_points.isEmpty() && !m_show_image))
        apply_image_zoom(1.0 / 1.15, 1.0);
    viewport()->update();
}

void ImageView::reset_zoom() {
    m_user_zoomed = false;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_zoom_factor = 1.0;
    m_zoom_x = 1.0; m_zoom_y = 1.0;
    m_pan_x = 0.0; m_pan_y = 0.0;
    m_contour_zoom = 1.0;
    m_contour_pan_x = 0.0;
    m_contour_pan_z = 0.0;
    viewport()->update();
}

void ImageView::apply_image_zoom(double sx, double sy) {
    QWidget* vp = viewport();
    double cx = vp->width() / 2.0;
    double cy = vp->height() / 2.0;
    QPointF old_scene = mapToScene(QPoint(static_cast<int>(cx), static_cast<int>(cy)));
    scale(sx, sy);
    QPointF new_scene = mapToScene(QPoint(static_cast<int>(cx), static_cast<int>(cy)));
    QPointF delta = new_scene - old_scene;
    translate(delta.x(), delta.y());
}

// ── Coordinate helpers ──

QVector<double> ImageView::get_eff_contour_range(double l, double t, double r, double b) const {
    if (!m_contour_range_valid) return {-48.0, 48.0, 50.0, 190.0};

    double x_min = m_contour_x_range.first, x_max = m_contour_x_range.second;
    double z_min = m_contour_z_range.first, z_max = m_contour_z_range.second;

    double zf_x = m_contour_zoom * m_zoom_x;
    double zf_z = m_contour_zoom * m_zoom_y;

    double x_center = (x_min + x_max) / 2.0 + m_contour_pan_x;
    double x_half = (x_max - x_min) / 2.0 / zf_x;
    double z_center = (z_min + z_max) / 2.0 + m_contour_pan_z;
    double z_half = (z_max - z_min) / 2.0 / zf_z;

    return {x_center - x_half, x_center + x_half,
            z_center - z_half, z_center + z_half};
}

double ImageView::mm_to_px_x(double x_mm, double l, double t, double r, double b) const {
    auto rng = get_eff_contour_range(l, t, r, b);
    double x_min = rng[0], x_max = rng[1];
    if (std::abs(x_max - x_min) < 1e-10) return l + (r - l) * 0.5;
    return l + (x_mm - x_min) / (x_max - x_min) * (r - l);
}

double ImageView::px_to_mm_x(double px, double l, double t, double r, double b) const {
    auto rng = get_eff_contour_range(l, t, r, b);
    double x_min = rng[0], x_max = rng[1];
    if (std::abs(r - l) < 1e-10) return 0.0;
    return x_min + (px - l) / (r - l) * (x_max - x_min);
}

double ImageView::mm_to_px_z(double z_mm, double l, double t, double r, double b) const {
    auto rng = get_eff_contour_range(l, t, r, b);
    double z_min = rng[2], z_max = rng[3];
    if (std::abs(z_max - z_min) < 1e-10) return t + (b - t) * 0.5;
    return t + (z_max - z_mm) / (z_max - z_min) * (b - t);
}

double ImageView::px_to_mm_z(double py, double l, double t, double r, double b) const {
    auto rng = get_eff_contour_range(l, t, r, b);
    double z_min = rng[2], z_max = rng[3];
    if (std::abs(b - t) < 1e-10) return 0.0;
    return z_max - (py - t) / (b - t) * (z_max - z_min);
}

void ImageView::sync_measure_pixels() {
    QWidget* vp = viewport();
    int gl = m_margin_left, gt = m_margin_top;
    int gr = vp->width() - m_margin_right, gb = vp->height() - m_margin_bottom;
    for (int i = 0; i < 2; i++) {
        m_measure_vlines[i] = mm_to_px_x(m_measure_vlines_mm[i], gl, gt, gr, gb);
        m_measure_hlines[i] = mm_to_px_z(m_measure_hlines_mm[i], gl, gt, gr, gb);
    }
}

// ── Event handlers ──

void ImageView::wheelEvent(QWheelEvent* event) {
    if (m_show_3d) { event->accept(); return; }

    bool has_contour = m_show_contour && !m_contour_points.isEmpty();
    bool has_image = !m_pixmap.isNull() && m_show_image;

    double delta = event->angleDelta().y();
    if (delta == 0) return;

    bool zoom_in = delta > 0;
    double mx = event->position().x();
    double my = event->position().y();

    int gl = m_margin_left, gr = viewport()->width() - m_margin_right;
    int gt = m_margin_top, gb = viewport()->height() - m_margin_bottom;
    const int axis_near = 30;

    if (has_contour && !has_image) {
        bool near_x_axis = std::abs(my - gb) < axis_near && gl <= mx && mx <= gr;
        bool near_z_axis = std::abs(mx - gl) < axis_near && gt <= my && my <= gb;

        if (near_x_axis) {
            double old_x = m_zoom_x;
            m_zoom_x = zoom_in ? std::min(m_zoom_x * 1.15, 60.0) : std::max(m_zoom_x / 1.15, 1.0);
            if (m_zoom_x != old_x) apply_contour_zoom_wheel(mx, my, old_x, m_zoom_x, "x");
        } else if (near_z_axis) {
            double old_y = m_zoom_y;
            m_zoom_y = zoom_in ? std::min(m_zoom_y * 1.15, 60.0) : std::max(m_zoom_y / 1.15, 1.0);
            if (m_zoom_y != old_y) apply_contour_zoom_wheel(mx, my, old_y, m_zoom_y, "z");
        } else {
            double old_z = m_contour_zoom;
            m_contour_zoom = zoom_in ? std::min(m_contour_zoom * 1.15, 15.0) : std::max(m_contour_zoom / 1.15, 1.0);
            if (m_contour_zoom != old_z) apply_contour_zoom_wheel(mx, my, old_z, m_contour_zoom, "both");
        }
        event->accept();
        viewport()->update();
        return;
    }

    if (has_image) {
        bool near_x_axis = std::abs(my - (viewport()->height() - 1)) < axis_near;
        bool near_y_axis = std::abs(mx) < axis_near;

        if (near_x_axis) {
            double old_y = m_zoom_y;
            m_zoom_y = zoom_in ? std::min(m_zoom_y * 1.15, 60.0) : std::max(m_zoom_y / 1.15, 1.0);
            if (m_zoom_y != old_y) { m_user_zoomed = true; apply_image_zoom(1.0, m_zoom_y / old_y); }
        } else if (near_y_axis) {
            double old_x = m_zoom_x;
            m_zoom_x = zoom_in ? std::min(m_zoom_x * 1.15, 60.0) : std::max(m_zoom_x / 1.15, 1.0);
            if (m_zoom_x != old_x) { m_user_zoomed = true; apply_image_zoom(m_zoom_x / old_x, 1.0); }
        } else {
            double factor = zoom_in ? 1.15 : 1.0 / 1.15;
            double new_zoom = m_zoom_factor * factor;
            if (m_min_zoom <= new_zoom && new_zoom <= m_max_zoom) {
                double old_z = m_zoom_factor;
                m_zoom_factor = new_zoom;
                m_user_zoomed = true;
                QPointF mouse_pos = event->position();
                QPointF old_scene = mapToScene(mouse_pos.toPoint());
                scale(factor, factor);
                QPointF new_scene = mapToScene(mouse_pos.toPoint());
                QPointF delta_s = new_scene - old_scene;
                translate(delta_s.x(), delta_s.y());
            }
        }
        event->accept();
        viewport()->update();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_pan_start = event->position();
        m_pan_start_x = m_pan_x; m_pan_start_y = m_pan_y;
        m_contour_pan_start_x = m_contour_pan_x;
        m_contour_pan_start_z = m_contour_pan_z;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && !m_measure_mode.isEmpty()) {
        double mx = event->position().x(), my = event->position().y();
        int gl = m_margin_left, gr = viewport()->width() - m_margin_right;
        int gt = m_margin_top, gb = viewport()->height() - m_margin_bottom;
        const int threshold = 8;

        for (int i = 0; i < 2; i++) {
            if (!m_measure_vlines_visible[i]) continue;
            if (std::abs(mx - m_measure_vlines[i]) < threshold && gt <= my && my <= gb) {
                m_dragging_measure_line = i;
                m_dragging_measure_type = "vertical";
                setCursor(Qt::SizeHorCursor);
                event->accept(); return;
            }
        }
        for (int i = 0; i < 2; i++) {
            if (!m_measure_hlines_visible[i]) continue;
            if (std::abs(my - m_measure_hlines[i]) < threshold && gl <= mx && mx <= gr) {
                m_dragging_measure_line = i;
                m_dragging_measure_type = "horizontal";
                setCursor(Qt::SizeVerCursor);
                event->accept(); return;
            }
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging_measure_line >= 0) {
        double mx = event->position().x(), my = event->position().y();
        int gl = m_margin_left, gr = viewport()->width() - m_margin_right;
        int gt = m_margin_top, gb = viewport()->height() - m_margin_bottom;

        if (m_dragging_measure_type == "vertical") {
            double vx = CLAMP(mx, gl, gr);
            m_measure_vlines_mm[m_dragging_measure_line] = px_to_mm_x(vx, gl, gt, gr, gb);
            m_measure_vlines[m_dragging_measure_line] = vx;
        } else if (m_dragging_measure_type == "horizontal") {
            double hy = CLAMP(my, gt, gb);
            m_measure_hlines_mm[m_dragging_measure_line] = px_to_mm_z(hy, gl, gt, gr, gb);
            m_measure_hlines[m_dragging_measure_line] = hy;
        }
        viewport()->update();
        event->accept();
        return;
    }

    if (m_panning) {
        QPointF delta = event->position() - m_pan_start;

        if (m_show_contour && !m_contour_points.isEmpty() && !m_show_image) {
            int vp_w = viewport()->width(), vp_h = viewport()->height();
            int gl = m_margin_left, gr = vp_w - m_margin_right;
            int gt = m_margin_top, gb = vp_h - m_margin_bottom;
            int gw = gr - gl, gh = gb - gt;

            if (gw > 0 && gh > 0 && m_contour_range_valid) {
                double x_min = m_contour_x_range.first, x_max = m_contour_x_range.second;
                double z_min = m_contour_z_range.first, z_max = m_contour_z_range.second;
                double zf_x = m_contour_zoom * m_zoom_x;
                double zf_z = m_contour_zoom * m_zoom_y;
                double x_half = (x_max - x_min) / 2.0 / (zf_x != 0 ? zf_x : 1);
                double z_half = (z_max - z_min) / 2.0 / (zf_z != 0 ? zf_z : 1);

                m_contour_pan_x = m_contour_pan_start_x - delta.x() / gw * (2.0 * x_half);
                m_contour_pan_z = m_contour_pan_start_z + delta.y() / gh * (2.0 * z_half);
            }
        } else {
            m_pan_start = event->position();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - static_cast<int>(delta.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() - static_cast<int>(delta.y()));
        }
        event->accept();
        viewport()->update();
        return;
    }

    if (m_show_3d && !m_batch_lines.isEmpty()) {
        double mx = event->position().x(), my = event->position().y();
        int gl = m_margin_left, gr = viewport()->width() - m_margin_right;
        int gt = m_margin_top, gb = viewport()->height() - m_margin_bottom;

        if (gl <= mx && mx <= gr && gt <= my && my <= gb && m_batch_range_valid) {
            double x_min = m_batch_x_range.first, x_max = m_batch_x_range.second;
            int total_lines = get_3d_total_lines();
            int line_idx = static_cast<int>((my - gt) / (gb - gt) * total_lines);
            double x_mm = x_min + (mx - gl) / (gr - gl) * (x_max - x_min);
            double z_mm = 0;
            bool found = false;

            int actual = m_batch_lines.size();
            if (line_idx >= 0 && line_idx < actual) {
                auto& line_data = m_batch_lines[line_idx].second;
                double best_dist = 1e10;
                for (const auto& pt : line_data) {
                    double fx = gl + (pt.first - x_min) / (x_max - x_min) * (gr - gl);
                    double dist = std::abs(fx - mx);
                    if (dist < best_dist) { best_dist = dist; z_mm = pt.second; found = true; }
                }
            }
            m_last_3d_mouse = {static_cast<double>(line_idx), x_mm, found ? z_mm : 0.0};
            viewport()->update();
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (m_dragging_measure_line >= 0) {
        m_dragging_measure_line = -1;
        m_dragging_measure_type.clear();
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        reset_zoom();
    } else {
        QGraphicsView::mouseDoubleClickEvent(event);
    }
}

void ImageView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    if (!m_pixmap.isNull() && !m_user_zoomed) {
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        m_zoom_factor = 1.0;
    }
}

void ImageView::scrollContentsBy(int dx, int dy) {
    if (m_show_contour && !m_contour_points.isEmpty() && !m_show_image) return;
    QGraphicsView::scrollContentsBy(dx, dy);
}

// ── Drawing ──

void ImageView::drawForeground(QPainter* painter, const QRectF& /*rect*/) {
    if (!m_camera_label.isEmpty()) draw_camera_label(*painter);

    if (m_show_3d) {
        draw_3d_heatmap(*painter);
        return;
    }
    if (!m_show_contour) return;
    if (m_show_image) return;

    painter->save();
    painter->setTransform(viewportTransform().inverted(), true);
    painter->setRenderHint(QPainter::Antialiasing, false);

    int vp_w = viewport()->width(), vp_h = viewport()->height();
    int gl = m_margin_left, gt = m_margin_top;
    int gr = vp_w - m_margin_right, gb = vp_h - m_margin_bottom;
    double tick_spacing = 60.0;

    draw_grid(*painter, gl, gt, gr, gb, tick_spacing);
    draw_axis(*painter, gl, gt, gr, gb);
    draw_axis_labels(*painter, gl, gt, gr, gb, tick_spacing);
    draw_contour_curve(*painter, gl, gt, gr, gb);
    draw_contour_info(*painter, gl, gt, gr, gb);

    painter->restore();
}

void ImageView::paintEvent(QPaintEvent* event) {
    QGraphicsView::paintEvent(event);

    if (m_measure_mode.isEmpty() || !m_show_contour || m_show_3d) return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);

    int vp_w = viewport()->width(), vp_h = viewport()->height();
    int gl = m_margin_left, gt = m_margin_top;
    int gr = vp_w - m_margin_right, gb = vp_h - m_margin_bottom;

    sync_measure_pixels();

    QColor colors[] = {QColor("#FF6B6B"), QColor("#4ECDC4")};
    for (int i = 0; i < 2; i++) {
        if (!m_measure_vlines_visible[i]) continue;
        QPen pen(colors[i], 2, Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF(m_measure_vlines[i], gt), QPointF(m_measure_vlines[i], gb));
    }
    for (int i = 0; i < 2; i++) {
        if (!m_measure_hlines_visible[i]) continue;
        QPen pen(colors[i], 2, Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(QPointF(gl, m_measure_hlines[i]), QPointF(gr, m_measure_hlines[i]));
    }
    draw_measure_panel(painter, gl, gt, gr, gb);
    painter.end();
}

void ImageView::draw_camera_label(QPainter& painter) {
    QFont font("Consolas", 12);
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(font);
    int pad = 6, bx = 8, by = 8;
    int tw = fm.horizontalAdvance(m_camera_label);
    int th = fm.height();
    painter.setPen(QPen(QColor("#00FFCC"), 1));
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRect(bx, by, tw + pad * 2, th + pad * 2);
    painter.setPen(QPen(QColor("#00FFCC"), 1));
    painter.drawText(bx + pad, by + pad + th - fm.descent(), m_camera_label);
}

void ImageView::draw_grid(QPainter& painter, double l, double t, double r, double b, double ts) {
    QPen grid_pen(QColor("#1A2E2E"), 1, Qt::DashLine);
    painter.setPen(grid_pen);
    double cx = (l + r) / 2.0, cy = (t + b) / 2.0;

    for (double x = cx; x < r; x += ts) painter.drawLine(QPointF(x, t), QPointF(x, b));
    for (double x = cx - ts; x > l; x -= ts) painter.drawLine(QPointF(x, t), QPointF(x, b));
    for (double y = cy; y < b; y += ts) painter.drawLine(QPointF(l, y), QPointF(r, y));
    for (double y = cy - ts; y > t; y -= ts) painter.drawLine(QPointF(l, y), QPointF(r, y));
}

void ImageView::draw_axis(QPainter& painter, double l, double t, double r, double b) {
    QPen axis_pen(QColor("#00CCAA"), 1.5);
    painter.setPen(axis_pen);
    painter.drawLine(QPointF(l, b), QPointF(r, b));
    painter.drawLine(QPointF(l, t), QPointF(l, b));
    double arrow = 5;
    painter.drawLine(QPointF(r - arrow, b - 3), QPointF(r, b));
    painter.drawLine(QPointF(r - arrow, b + 3), QPointF(r, b));
}

void ImageView::draw_contour_curve(QPainter& painter, double l, double t, double r, double b) {
    if (m_contour_points.isEmpty()) return;
    auto rng = get_eff_contour_range(l, t, r, b);
    double x_min = rng[0], x_max = rng[1], z_min = rng[2], z_max = rng[3];

    double scale_x = (x_max != x_min) ? (r - l) / (x_max - x_min) : 1.0;
    double scale_z = (z_max != z_min) ? (b - t) / (z_max - z_min) : 1.0;

    QPen pen(QColor("#00FF88"), 1.5);
    painter.setPen(pen);

    double prev_wx = 0, prev_wy = 0;
    bool has_prev = false;
    for (const auto& pt : m_contour_points) {
        double wx = l + (pt.first - x_min) * scale_x;
        double wy = b - (pt.second - z_min) * scale_z;
        if (has_prev)
            painter.drawLine(QPointF(prev_wx, prev_wy), QPointF(wx, wy));
        prev_wx = wx; prev_wy = wy; has_prev = true;
    }
}

void ImageView::draw_measure_panel(QPainter& painter, double l, double t, double r, double b) {
    QFont font("Consolas", 9);
    painter.setFont(font);

    QStringList lines;
    if (m_measure_vlines_visible[0]) lines.append(QString("V1: %1 mm").arg(m_measure_vlines_mm[0], 0, 'f', 3));
    if (m_measure_vlines_visible[1]) lines.append(QString("V2: %1 mm").arg(m_measure_vlines_mm[1], 0, 'f', 3));
    if (m_measure_vlines_visible[0] && m_measure_vlines_visible[1])
        lines.append(QString("ΔV: %1 mm").arg(std::abs(m_measure_vlines_mm[1] - m_measure_vlines_mm[0]), 0, 'f', 3));
    if (m_measure_hlines_visible[0]) lines.append(QString("H1: %1 mm").arg(m_measure_hlines_mm[0], 0, 'f', 3));
    if (m_measure_hlines_visible[1]) lines.append(QString("H2: %1 mm").arg(m_measure_hlines_mm[1], 0, 'f', 3));
    if (m_measure_hlines_visible[0] && m_measure_hlines_visible[1])
        lines.append(QString("ΔH: %1 mm").arg(std::abs(m_measure_hlines_mm[1] - m_measure_hlines_mm[0]), 0, 'f', 3));

    if (lines.isEmpty()) return;

    double panel_w = 160, panel_h = 20 + lines.size() * 18;
    double px = r - panel_w - 10, py = b - panel_h - 10;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRect(QRectF(px, py, panel_w, panel_h));

    painter.setPen(QPen(QColor("#00FFCC"), 1));
    QFont cf("Consolas", 9);
    painter.setFont(cf);
    for (int i = 0; i < lines.size(); i++)
        painter.drawText(QPointF(px + 10, py + 16 + i * 18), lines[i]);
}

void ImageView::draw_axis_labels(QPainter& painter, double l, double t, double r, double b, double ts) {
    auto rng = get_eff_contour_range(l, t, r, b);
    double x_min = rng[0], x_max = rng[1], z_min = rng[2], z_max = rng[3];

    QFont font("Consolas", 8);
    painter.setFont(font);
    painter.setPen(QPen(QColor("#00FFCC"), 1));
    QFontMetrics fm(font);

    double cx = (l + r) / 2.0, cy = (t + b) / 2.0;
    double x_range = x_max - x_min, z_range = z_max - z_min;
    double x_step_mm = (r != l) ? x_range / (r - l) * ts : 0;
    double z_step_mm = (b != t) ? z_range / (b - t) * ts : 0;

    // X axis labels
    for (double x = cx; x <= r; x += ts) {
        double mm = x_min + (x - l) / (r - l) * x_range;
        QString text = fmt_mm(mm, x_step_mm);
        int tw = fm.horizontalAdvance(text);
        painter.drawLine(QPointF(x, b), QPointF(x, b + 4));
        painter.drawText(static_cast<int>(x - tw / 2), static_cast<int>(b + 16), text);
    }
    for (double x = cx - ts; x >= l; x -= ts) {
        double mm = x_min + (x - l) / (r - l) * x_range;
        QString text = fmt_mm(mm, x_step_mm);
        int tw = fm.horizontalAdvance(text);
        painter.drawLine(QPointF(x, b), QPointF(x, b + 4));
        painter.drawText(static_cast<int>(x - tw / 2), static_cast<int>(b + 16), text);
    }

    // Z axis labels
    for (double y = cy; y >= t; y -= ts) {
        double mm = z_max - (y - t) / (b - t) * z_range;
        QString text = fmt_mm(mm, z_step_mm);
        int tw = fm.horizontalAdvance(text);
        painter.drawLine(QPointF(l - 4, y), QPointF(l, y));
        painter.drawText(static_cast<int>(l - tw - 6), static_cast<int>(y + fm.height() / 3), text);
    }
    for (double y = cy + ts; y <= b; y += ts) {
        double mm = z_max - (y - t) / (b - t) * z_range;
        QString text = fmt_mm(mm, z_step_mm);
        int tw = fm.horizontalAdvance(text);
        painter.drawLine(QPointF(l - 4, y), QPointF(l, y));
        painter.drawText(static_cast<int>(l - tw - 6), static_cast<int>(y + fm.height() / 3), text);
    }

    QFont lf("Segoe UI", 9, QFont::Bold);
    painter.setFont(lf);
    painter.drawText(static_cast<int>(r - 15), static_cast<int>(b - 8), "X");
    painter.drawText(static_cast<int>(l + 8), static_cast<int>(t + 12), "Z");
    painter.drawText(static_cast<int>(r - 35), static_cast<int>(b + 32), "(mm)");
    painter.drawText(static_cast<int>(l - 50), static_cast<int>(t - 6), "(mm)");
}

QString ImageView::fmt_mm(double val, double step) {
    if (std::abs(val) < 1e-7) return "0";
    if (step > 0) {
        int decimals = std::max(1, -static_cast<int>(std::floor(std::log10(step))) + 1);
        decimals = std::min(decimals, 6);
        return QString::number(val, 'f', decimals);
    }
    if (std::abs(val) >= 100) return QString::number(val, 'f', 1);
    if (std::abs(val) >= 10) return QString::number(val, 'f', 2);
    return QString::number(val, 'f', 3);
}

void ImageView::draw_axis_labels_3d(QPainter& painter, double l, double t, double r, double b) {
    QFont font("Consolas", 8);
    painter.setFont(font);
    painter.setPen(QPen(QColor("#00FFCC"), 1));
    QFontMetrics fm(font);

    int total_lines = get_3d_total_lines();
    double tick_spacing = 60.0;

    if (m_batch_range_valid) {
        double x_min = m_batch_x_range.first, x_max = m_batch_x_range.second;
        double x_range = x_max - x_min;
        if (x_range > 0) {
            for (double x = std::round(l / tick_spacing) * tick_spacing; x <= r; x += tick_spacing) {
                double mm = x_min + (x - l) / (r - l) * x_range;
                QString text = fmt_mm(mm, x_range / (r - l) * tick_spacing);
                int tw = fm.horizontalAdvance(text);
                painter.drawLine(QPointF(x, b), QPointF(x, b + 4));
                painter.drawText(static_cast<int>(x - tw / 2), static_cast<int>(b + 16), text);
            }
        }
    }

    int y_ticks = std::min(6, total_lines);
    int y_step = std::max(1, total_lines / y_ticks);
    for (int i = 0; i <= total_lines; i += y_step) {
        double fy = t + i * (b - t) / total_lines;
        QString text = QString::number(i);
        int tw = fm.horizontalAdvance(text);
        painter.drawLine(QPointF(l - 4, fy), QPointF(l, fy));
        painter.drawText(static_cast<int>(l - tw - 6), static_cast<int>(fy + fm.height() / 3), text);
    }

    // Labels
    QString x_label = "X (mm)";
    painter.drawText(QRectF(l, b + 5, r - l, 20), Qt::AlignCenter, x_label);

    painter.save();
    painter.translate(l - 44, (t + b) / 2);
    painter.rotate(-90);
    QString y_label = "Line#";
    painter.drawText(QRectF(-50, -10, 100, 20), Qt::AlignCenter, y_label);
    painter.restore();
}

void ImageView::draw_3d_coord_info(QPainter& painter, double gr, double gb) {
    if (m_last_3d_mouse.size() < 3) return;

    int line_idx = static_cast<int>(m_last_3d_mouse[0]);
    double x_mm = m_last_3d_mouse[1];
    double z_mm = m_last_3d_mouse[2];

    QFont font("Consolas", 9);
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(font);

    QStringList lines;
    lines << QString("X: %1 mm").arg(x_mm, 0, 'f', 2)
          << QString("Y: %1").arg(line_idx)
          << QString("Z: %1 mm").arg(z_mm, 0, 'f', 2);

    int line_h = fm.height() + 2;
    int max_w = 0;
    for (const auto& s : lines)
        max_w = std::max(max_w, fm.horizontalAdvance(s));
    int pad = 6;
    double box_w = max_w + pad * 2;
    double box_h = lines.size() * line_h + pad * 2;
    double box_x = gr - box_w - 8;
    double box_y = gb - box_h - 8;

    painter.setPen(QPen(QColor("#00FFCC"), 1));
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRect(QRectF(box_x, box_y, box_w, box_h));

    painter.setPen(QPen(QColor("#00FFCC"), 1));
    for (int i = 0; i < lines.size(); i++)
        painter.drawText(QPointF(box_x + pad, box_y + pad + (i + 1) * line_h - 4), lines[i]);
}

void ImageView::draw_contour_info(QPainter& painter, double l, double t, double r, double b) {
    if (!m_measure_mode.isEmpty())
        draw_measure_info_panel(painter, l, t, r, b);
}

void ImageView::draw_measure_info_panel(QPainter& painter, double l, double t, double r, double b) {
    sync_measure_pixels();
    QFont font("Consolas", 9);
    painter.setFont(font);

    QString colors[] = {"#FF6B6B", "#4ECDC4"};
    QStringList lines;

    for (int i = 0; i < 2; i++) {
        if (m_measure_vlines_visible[i] && l <= m_measure_vlines[i] && m_measure_vlines[i] <= r) {
            QPen pen(QColor(colors[i]), 1.5, Qt::DashLine);
            painter.setPen(pen);
            painter.drawLine(QPointF(m_measure_vlines[i], t), QPointF(m_measure_vlines[i], b));
            lines.append(QString("V%1: %2 mm").arg(i + 1).arg(m_measure_vlines_mm[i], 0, 'f', 3));
        }
    }
    if (m_measure_vlines_visible[0] && m_measure_vlines_visible[1])
        lines.append(QString("ΔV: %1 mm").arg(std::abs(m_measure_vlines_mm[1] - m_measure_vlines_mm[0]), 0, 'f', 3));
    for (int i = 0; i < 2; i++) {
        if (m_measure_hlines_visible[i] && t <= m_measure_hlines[i] && m_measure_hlines[i] <= b) {
            QPen pen(QColor(colors[i]), 1.5, Qt::DashLine);
            painter.setPen(pen);
            painter.drawLine(QPointF(l, m_measure_hlines[i]), QPointF(r, m_measure_hlines[i]));
            lines.append(QString("H%1: %2 mm").arg(i + 1).arg(m_measure_hlines_mm[i], 0, 'f', 3));
        }
    }
    if (m_measure_hlines_visible[0] && m_measure_hlines_visible[1])
        lines.append(QString("ΔH: %1 mm").arg(std::abs(m_measure_hlines_mm[1] - m_measure_hlines_mm[0]), 0, 'f', 3));

    if (lines.isEmpty()) return;

    double panel_w = 160, panel_h = 20 + lines.size() * 18;
    double px = r - panel_w - 10, py = b - panel_h - 10;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRect(QRectF(px, py, panel_w, panel_h));

    painter.setPen(QPen(QColor("#00FFCC"), 1));
    painter.setFont(QFont("Consolas", 9));
    for (int i = 0; i < lines.size(); i++)
        painter.drawText(QPointF(px + 10, py + 16 + i * 18), lines[i]);
}

void ImageView::draw_3d_heatmap(QPainter& painter) {
    painter.save();
    painter.setTransform(viewportTransform().inverted(), true);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int vp_w = viewport()->width(), vp_h = viewport()->height();
    int gl = m_margin_left, gt = m_margin_top;
    int gr = vp_w - m_margin_right, gb = vp_h - m_margin_bottom;

    if (m_batch_lines.isEmpty() || !m_batch_range_valid) {
        draw_axis(painter, gl, gt, gr, gb);
        draw_axis_labels_3d(painter, gl, gt, gr, gb);
        painter.restore();
        return;
    }

    double x_min = m_batch_x_range.first, x_max = m_batch_x_range.second;
    double z_min = m_batch_z_range.first, z_max = m_batch_z_range.second;
    if (x_max <= x_min || z_max <= z_min) { painter.restore(); return; }

    int actual_lines = m_batch_lines.size();
    int img_w = 512, img_h = actual_lines;
    if (img_h < 1) { painter.restore(); return; }

    // Build 2D image from batch data
    std::vector<std::vector<float>> z_img(img_h, std::vector<float>(img_w, std::nanf("")));

    for (int row = 0; row < img_h; row++) {
        auto& line = m_batch_lines[row].second;
        if (line.size() < 2) continue;

        std::vector<double> xs(line.size()), zs(line.size());
        for (int k = 0; k < line.size(); k++) {
            xs[k] = line[k].first;
            zs[k] = line[k].second;
        }

        // Manual linear interpolation
        for (int col = 0; col < img_w; col++) {
            double x_val = x_min + (x_max - x_min) * col / (img_w - 1);
            // Find surrounding points and interpolate
            if (x_val <= xs[0]) {
                z_img[row][col] = zs[0];
            } else if (x_val >= xs.back()) {
                z_img[row][col] = zs.back();
            } else {
                int idx = 0;
                for (int k = 0; k < xs.size() - 1; k++) {
                    if (x_val >= xs[k] && x_val <= xs[k + 1]) { idx = k; break; }
                }
                double t = (xs[idx + 1] != xs[idx])
                    ? (x_val - xs[idx]) / (xs[idx + 1] - xs[idx]) : 0.0;
                z_img[row][col] = static_cast<float>(zs[idx] + t * (zs[idx + 1] - zs[idx]));
            }
        }
    }

    // Normalize to 0-255
    double z_range = z_max - z_min;
    if (z_range < 1e-6) z_range = 1e-6;

    std::vector<uint8_t> gray(img_w * img_h);
    for (int row = 0; row < img_h; row++) {
        for (int col = 0; col < img_w; col++) {
            float v = z_img[row][col];
            float norm = std::isnan(v) ? 0.0f : (v - z_min) / z_range;
            norm = CLAMP(norm, 0.0f, 1.0f);
            gray[row * img_w + col] = static_cast<uint8_t>(norm * 255.0f);
        }
    }

    QImage qimg(gray.data(), img_w, img_h, img_w, QImage::Format_Grayscale8);
    QPixmap pix = QPixmap::fromImage(qimg);

    int total_lines = get_3d_total_lines();
    double draw_h = (gb - gt) * actual_lines / static_cast<double>(total_lines);
    QRectF target_rect(gl, gt, gr - gl, draw_h);

    painter.drawPixmap(target_rect, pix, QRectF(0, 0, img_w, img_h));
    draw_axis(painter, gl, gt, gr, gb);
    draw_axis_labels_3d(painter, gl, gt, gr, gb);
    draw_3d_coord_info(painter, gr, gb);
    painter.restore();
}

void ImageView::sync_contour_scrollbars() {
    if (!m_contour_range_valid || m_contour_points.isEmpty() || m_show_image || m_show_3d)
        return;

    double x_min = m_contour_x_range.first, x_max = m_contour_x_range.second;
    double z_min = m_contour_z_range.first, z_max = m_contour_z_range.second;
    double zf_x = m_contour_zoom * m_zoom_x;
    double zf_z = m_contour_zoom * m_zoom_y;

    double x_center = (x_min + x_max) / 2.0 + m_contour_pan_x;
    double x_half = (x_max - x_min) / 2.0 / (zf_x != 0 ? zf_x : 1);
    double z_center = (z_min + z_max) / 2.0 + m_contour_pan_z;
    double z_half = (z_max - z_min) / 2.0 / (zf_z != 0 ? zf_z : 1);

    double eff_x_min = x_center - x_half, eff_x_max = x_center + x_half;
    double eff_z_min = z_center - z_half, eff_z_max = z_center + z_half;
    double total_x = x_max - x_min, total_z = z_max - z_min;

    const double SCALE = 100000.0;

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    int vp_w = viewport()->width(), vp_h = viewport()->height();
    double scene_w = std::max(total_x * SCALE, static_cast<double>(vp_w + 1));
    double scene_h = std::max(total_z * SCALE, static_cast<double>(vp_h + 1));
    m_scene->setSceneRect(QRectF(0, 0, scene_w, scene_h));

    QScrollBar* h_bar = horizontalScrollBar();
    QScrollBar* v_bar = verticalScrollBar();

    h_bar->blockSignals(true);
    v_bar->blockSignals(true);

    int page_step_x = std::max(1, static_cast<int>((eff_x_max - eff_x_min) * SCALE));
    int page_step_z = std::max(1, static_cast<int>((eff_z_max - eff_z_min) * SCALE));

    h_bar->setRange(0, std::max(0, static_cast<int>(total_x * SCALE) - page_step_x));
    h_bar->setPageStep(page_step_x);
    h_bar->setValue(static_cast<int>(std::max(0.0, (eff_x_min - x_min) * SCALE)));

    v_bar->setRange(0, std::max(0, static_cast<int>(total_z * SCALE) - page_step_z));
    v_bar->setPageStep(page_step_z);
    v_bar->setValue(static_cast<int>(std::max(0.0, (eff_z_min - z_min) * SCALE)));

    h_bar->blockSignals(false);
    v_bar->blockSignals(false);
}

void ImageView::apply_contour_zoom_wheel(double mx, double my, double old_zoom, double new_zoom, const QString& axis) {
    if (!m_contour_range_valid) return;

    int gl = m_margin_left, gr = viewport()->width() - m_margin_right;
    int gt = m_margin_top, gb = viewport()->height() - m_margin_bottom;
    int gw = gr - gl, gh = gb - gt;

    double x_min = m_contour_x_range.first, x_max = m_contour_x_range.second;
    double z_min = m_contour_z_range.first, z_max = m_contour_z_range.second;
    double x_data_center = (x_min + x_max) / 2.0;
    double z_data_center = (z_min + z_max) / 2.0;

    double old_zf_x, old_zf_z, new_zf_x, new_zf_z;
    if (axis == "both") {
        old_zf_x = old_zoom * m_zoom_x; old_zf_z = old_zoom * m_zoom_y;
        new_zf_x = new_zoom * m_zoom_x; new_zf_z = new_zoom * m_zoom_y;
    } else if (axis == "x") {
        old_zf_x = old_zoom * m_zoom_x; old_zf_z = m_contour_zoom * m_zoom_y;
        new_zf_x = new_zoom * m_zoom_x; new_zf_z = m_contour_zoom * m_zoom_y;
    } else {
        old_zf_x = m_contour_zoom * m_zoom_x; old_zf_z = old_zoom * m_zoom_y;
        new_zf_x = m_contour_zoom * m_zoom_x; new_zf_z = new_zoom * m_zoom_y;
    }

    double old_x_half = (x_max - x_min) / 2.0 / (old_zf_x != 0 ? old_zf_x : 1);
    double old_z_half = (z_max - z_min) / 2.0 / (old_zf_z != 0 ? old_zf_z : 1);
    double old_eff_cx = x_data_center + m_contour_pan_x;
    double old_eff_cz = z_data_center + m_contour_pan_z;
    double old_eff_x_min = old_eff_cx - old_x_half;
    double old_eff_z_min = old_eff_cz - old_z_half;

    double mm_x = (gw > 0) ? old_eff_x_min + (mx - gl) / gw * (2.0 * old_x_half) : x_data_center;
    double mm_z = (gh > 0) ? old_eff_z_min + (gb - my) / gh * (2.0 * old_z_half) : z_data_center;

    double new_x_half = (x_max - x_min) / 2.0 / (new_zf_x != 0 ? new_zf_x : 1);
    double new_z_half = (z_max - z_min) / 2.0 / (new_zf_z != 0 ? new_zf_z : 1);

    double ratio_x = (gw > 0) ? (mx - gl) / gw : 0.5;
    double ratio_z = (gh > 0) ? (gb - my) / gh : 0.5;

    m_contour_pan_x = mm_x - (x_data_center + 2.0 * (ratio_x - 0.5) * new_x_half);
    m_contour_pan_z = mm_z - (z_data_center + 2.0 * (ratio_z - 0.5) * new_z_half);
}

// ══════════════════════════════════════════════════
// StatusBarWidget
// ══════════════════════════════════════════════════

StatusBarWidget::StatusBarWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);

    m_lbl_sample = new QLabel("Sample Rate: 2500 Hz");
    m_lbl_ip = new QLabel("IP: ---");

    for (auto lbl : {m_lbl_sample, m_lbl_ip}) {
        lbl->setStyleSheet("color: #A0A0A0; padding: 0 8px; font-size: 12px;");
        layout->addWidget(lbl);
    }
    layout->addStretch();
}

void StatusBarWidget::set_sample_rate(const QString& text) {
    m_lbl_sample->setText(QString("Sample Rate: %1").arg(text));
}

void StatusBarWidget::set_ip(const QString& text) {
    m_lbl_ip->setText(QString("IP: %1").arg(text));
}
