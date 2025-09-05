// Tests for RecordingLoader using temporary on-disk structure
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <atomic>
#include "recording_loader.h"
#include <filesystem>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

// Helper to create a temp directory with minimal frame_cam0 images
static fs::path createTempRecordingDir(int frameCount = 3) {
    auto tmp = fs::temp_directory_path() / fs::path("ebv_loader_test_" + std::to_string(::getpid()) + "_" + std::to_string(rand()));
    fs::create_directories(tmp / "frame_cam0");
    // only one camera populated
    for (int i = 0; i < frameCount; ++i) {
        cv::Mat img(10,10,CV_8UC3, cv::Scalar(10*i, 0, 255-10*i));
        cv::imwrite((tmp/"frame_cam0"/("frame_"+std::to_string(i)+".jpg")).string(), img);
    }
    return tmp;
}

TEST(RecordingLoaderBasic, LoadsFrameOnlyRecording) {
    int argc = 0; char** argv = nullptr; // ensure QCoreApplication exists once
    if (!QCoreApplication::instance()) { new QCoreApplication(argc, argv); }

    auto dir = createTempRecordingDir();
    RecordingLoader loader;
    std::atomic<int> finishedCount{0};
    QObject::connect(&loader, &RecordingLoader::loadingFinished, [&finishedCount](bool, const QString&){ finishedCount++; });
    loader.loadRecording(dir.string());

    // Spin event loop until finished or timeout
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (finishedCount.load()==0 && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GT(finishedCount.load(), 0) << "Loading did not finish in time";
    ASSERT_TRUE(loader.isDataReady());
    const auto &data = loader.getData();
    EXPECT_TRUE(data.isValid);
    EXPECT_EQ(data.frameCams[0].image_files.size(), 3u);
    // Event cameras absent -> marked invalid
    EXPECT_FALSE(data.eventCams[0].isValid);
    EXPECT_GE(data.totalFrames, 3u);
}

TEST(RecordingLoaderBasic, MissingDirectoryFailsGracefully) {
    int argc = 0; char** argv = nullptr; if (!QCoreApplication::instance()) { new QCoreApplication(argc, argv); }
    RecordingLoader loader;
    std::atomic<int> finishedCount{0};
    QObject::connect(&loader, &RecordingLoader::loadingFinished, [&finishedCount](bool, const QString&){ finishedCount++; });
    loader.loadRecording("/nonexistent/path/that/should/not/exist_12345");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (finishedCount.load()==0 && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(finishedCount.load(), 0);
    EXPECT_FALSE(loader.isDataReady());
}
