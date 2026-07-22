#ifndef WIDGETS_H
#define WIDGETS_H

#include <QWidget>
#include <QLabel>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsPathItem>
#include <QPainter>
#include <QPixmap>
#include <QVector>
#include <QPair>
#include <cmath>
#include <opencv2/core.hpp>

#include "protocol/camera_frame.h"

// ══════════════════════════════════════════════════
// ImageView (ImageDisplayWidget) — QGraphicsView-based image display
// ══════════════════════════════════════════════════

class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    void set_calibration(double x_res, double z_res,
                         double center_x = -1, double ref_y = -1);
    double pixel_to_physical_x(double px) const;
    double pixel_to_physical_z(double py) const;

    void set_image(const cv::Mat& arr);
    void clear_image();
    QPixmap pixmap() const { return m_pixmap; }

    void set_camera_frame(const CameraFrame& frame);
    void set_camera_label(const QString& label);

    void set_show_image(bool enabled);
    void set_show_contour(bool enabled);
    void set_show_centerline(bool enabled);
    void set_show_3d(bool enabled);

    void set_contour_data(const QVector<QPair<float, float>>& points);
    void set_contour_image_points(const QVector<QPair<float, float>>& points);
    void clear_all();

    // Measure lines
    void set_measure_mode(const QString& mode);
    void set_measure_line_visible(const QString& mode, int index, bool visible);

    // Batch / 3D
    void start_batch();
    void set_batch_total_count(int count);
    void stop_batch();
    void reset_batch();
    void add_batch_line(const QVector<QPair<float, float>>& points);

    // Zoom
    void zoom_row_in();
    void zoom_row_out();
    void zoom_col_in();
    void zoom_col_out();
    void reset_zoom();
    void set_mode(const QString& /*mode*/) {}

signals:
    void frame_changed(const cv::Mat& img);
    void batch_complete();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Drawing helpers
    void draw_contour_curve(QPainter& painter, double l, double t, double r, double b);
    void draw_3d_heatmap(QPainter& painter);
    void draw_camera_label(QPainter& painter);
    void draw_grid(QPainter& painter, double l, double t, double r, double b, double ts);
    void draw_axis(QPainter& painter, double l, double t, double r, double b);
    void draw_axis_labels(QPainter& painter, double l, double t, double r, double b, double ts);
    void draw_axis_labels_3d(QPainter& painter, double l, double t, double r, double b);
    void draw_measure_panel(QPainter& painter, double l, double t, double r, double b);
    void draw_measure_info_panel(QPainter& painter, double l, double t, double r, double b);
    void draw_3d_coord_info(QPainter& painter, double gr, double gb);
    void draw_contour_info(QPainter& painter, double l, double t, double r, double b);

    // Coordinate helpers
    QVector<double> get_eff_contour_range(double l, double t, double r, double b) const;
    double mm_to_px_x(double x_mm, double l, double t, double r, double b) const;
    double px_to_mm_x(double px, double l, double t, double r, double b) const;
    double mm_to_px_z(double z_mm, double l, double t, double r, double b) const;
    double px_to_mm_z(double py, double l, double t, double r, double b) const;
    void sync_measure_pixels();
    void sync_contour_scrollbars();
    void apply_image_zoom(double sx, double sy);
    void apply_contour_zoom_wheel(double mx, double my, double old_zoom, double new_zoom, const QString& axis);
    void update_centerline_visibility();

    static QString fmt_mm(double val, double step = -1);
    int get_3d_total_lines() const;

    bool m_panning = false;
    QPointF m_pan_start;
    double m_pan_start_x = 0, m_pan_start_y = 0;
    double m_contour_pan_start_x = 0, m_contour_pan_start_z = 0;

    double m_zoom_factor = 1.0;
    double m_zoom_x = 1.0;
    double m_zoom_y = 1.0;
    double m_pan_x = 0.0;
    double m_pan_y = 0.0;
    double m_min_zoom = 1.0;
    double m_max_zoom = 100.0;
    bool m_user_zoomed = false;

    double m_x_resolution = 0.01;
    double m_z_resolution = 0.01;
    double m_center_x = 640.0;
    double m_reference_y = 512.0;

    int m_margin_left = 60;
    int m_margin_bottom = 45;
    int m_margin_right = 30;
    int m_margin_top = 20;

    QGraphicsScene* m_scene = nullptr;
    QPixmap m_pixmap;
    QGraphicsPixmapItem* m_image_item = nullptr;
    bool m_show_image = true;
    bool m_show_contour = false;
    bool m_show_centerline = false;
    bool m_show_3d = false;
    int m_image_w = 1280;
    int m_image_h = 1024;

    // Contour data
    QVector<QPair<float, float>> m_contour_points;
    QPair<float, float> m_contour_x_range;
    QPair<float, float> m_contour_z_range;
    bool m_contour_range_valid = false;
    double m_contour_zoom = 1.0;
    double m_contour_pan_x = 0.0;
    double m_contour_pan_z = 0.0;
    QVector<QPair<float, float>> m_contour_image_points;

    // Centerline
    QGraphicsPathItem* m_centerline_item = nullptr;

    // Measure lines
    QString m_measure_mode;
    QVector<double> m_measure_vlines = {0, 0};
    QVector<double> m_measure_hlines = {0, 0};
    QVector<double> m_measure_vlines_mm = {0, 0};
    QVector<double> m_measure_hlines_mm = {0, 0};
    QVector<bool> m_measure_vlines_visible = {false, false};
    QVector<bool> m_measure_hlines_visible = {false, false};
    int m_dragging_measure_line = -1;
    QString m_dragging_measure_type;

    // 3D batch
    QVector<QPair<int, QVector<QPair<float, float>>>> m_batch_lines;
    int m_batch_line_index = 0;
    bool m_batch_running = false;
    int m_batch_total_count = 0;
    QPair<float, float> m_batch_x_range;
    QPair<float, float> m_batch_z_range;
    bool m_batch_range_valid = false;
    QVector<double> m_last_3d_mouse; // {line_idx, x_mm, z_mm}

    QString m_camera_label;
};

// Alias for backward compat
using ImageDisplayWidget = ImageView;

// ══════════════════════════════════════════════════
// StatusBarWidget
// ══════════════════════════════════════════════════

class StatusBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatusBarWidget(QWidget* parent = nullptr);
    void set_sample_rate(const QString& text);
    void set_ip(const QString& text);

private:
    QLabel* m_lbl_sample = nullptr;
    QLabel* m_lbl_ip = nullptr;
};

#endif // WIDGETS_H
