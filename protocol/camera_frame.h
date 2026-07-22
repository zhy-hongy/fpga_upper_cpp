#ifndef CAMERA_FRAME_H
#define CAMERA_FRAME_H

#include <QVector>
#include <QPointF>
#include <QPair>
#include <opencv2/core.hpp>

struct CameraFrame {
    cv::Mat image;                                    // Grayscale image
    QVector<QPair<float, float>> profile;            // Contour data (x, z)
    QVector<QPair<float, float>> profile_image_points; // Image-space points
    bool has_image = false;
    bool has_profile = false;
    int sensor_id = 0;

    void set_image(const cv::Mat& arr) {
        image = arr.clone();
        has_image = true;
    }

    void set_profile(const QVector<QPair<float, float>>& points,
                     const QVector<QPair<float, float>>* image_points = nullptr) {
        profile = points;
        has_profile = !points.isEmpty();
        if (image_points) {
            profile_image_points = *image_points;
        }
    }

    void clear_image() {
        image = cv::Mat();
        has_image = false;
    }

    void clear_profile() {
        profile.clear();
        profile_image_points.clear();
        has_profile = false;
    }

    void clear() {
        clear_image();
        clear_profile();
    }
};

#endif // CAMERA_FRAME_H
