#include "utils_qt.h"

QImage cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) {
        return QImage();
    }
    
    switch (mat.type()) {
        case CV_8UC4: {
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_ARGB32).copy();
        }
        case CV_8UC3: {
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888).rgbSwapped().copy();
        }
        case CV_8UC1: {
            return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8).copy();
        }
        default:
            break;
    }
    
    // Convert to a supported format if needed
    cv::Mat converted;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, converted, cv::COLOR_BGR2RGB);
    return QImage(converted.data, converted.cols, converted.rows, converted.step, QImage::Format_RGB888).copy();
    } else if (mat.channels() == 1) {
    return QImage(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8).copy();
    }
    
    return QImage();
}
