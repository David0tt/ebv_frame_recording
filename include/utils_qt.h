#pragma once

#include <QImage>
#include <opencv2/opencv.hpp>

// Utility function to convert cv::Mat to QImage (Qt-specific)
QImage cvMatToQImage(const cv::Mat& mat);
