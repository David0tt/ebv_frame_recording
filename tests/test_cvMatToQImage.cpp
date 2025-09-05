#include <gtest/gtest.h>
#include <QImage>
#include <opencv2/opencv.hpp>
#include "utils_qt.h"

TEST(CvMatToQImage, ConvertsColorBGRToRGB) {
    cv::Mat bgr(1,1,CV_8UC3);
    bgr.at<cv::Vec3b>(0,0) = cv::Vec3b(10,20,30); // B,G,R
    QImage q = cvMatToQImage(bgr);
    ASSERT_FALSE(q.isNull());
    EXPECT_EQ(q.format(), QImage::Format_RGB888);
    QColor c = q.pixelColor(0,0);
    EXPECT_EQ(c.red(), 30);
    EXPECT_EQ(c.green(), 20);
    EXPECT_EQ(c.blue(), 10);
}

TEST(CvMatToQImage, ConvertsGray) {
    cv::Mat g(2,2,CV_8UC1, cv::Scalar(128));
    QImage q = cvMatToQImage(g);
    ASSERT_FALSE(q.isNull());
    EXPECT_EQ(q.format(), QImage::Format_Grayscale8);
}

TEST(CvMatToQImage, EmptyReturnsNull) {
    cv::Mat empty;
    QImage q = cvMatToQImage(empty);
    EXPECT_TRUE(q.isNull());
}
