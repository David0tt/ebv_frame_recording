// Tests for RecordingBuffer
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <opencv2/opencv.hpp>
#include "recording_buffer.h"
#include "recording_loader.h"
#include "recording_manager.h" // for type used in RecordingBuffer live path
#include <filesystem>

namespace fs = std::filesystem;

// Minimal fake RecordingManager implementing only the methods used by RecordingBuffer live mode
class FakeLiveRecordingManager : public RecordingManager {
public:
    FakeLiveRecordingManager() : RecordingManager(nullptr, nullptr) {}
    // Public toggles to feed frames
    void pushFrame(int cam, const cv::Mat &img) {
        std::lock_guard<std::mutex> l(mutex_);
        frameImgs_[cam] = img.clone();
        frameIndices_[cam]++;
    }
    void pushEvent(int cam, const cv::Mat &img) {
        std::lock_guard<std::mutex> l(mutex_);
        eventImgs_[cam] = img.clone();
        eventIndices_[cam]++;
    }
    bool getLiveFrameData(int cameraId, cv::Mat &frame, size_t &frameIndex) {
        std::lock_guard<std::mutex> l(mutex_);
        if (frameImgs_[cameraId].empty()) return false;
        frame = frameImgs_[cameraId].clone();
        frameIndex = frameIndices_[cameraId];
        return true;
    }
    bool getLiveEventData(int cameraId, cv::Mat &eventFrame, size_t &frameIndex) {
        std::lock_guard<std::mutex> l(mutex_);
        if (eventImgs_[cameraId].empty()) return false;
        eventFrame = eventImgs_[cameraId].clone();
        frameIndex = eventIndices_[cameraId];
        return true;
    }
    bool isRecording() const { return true; }
private:
    mutable std::mutex mutex_;
    cv::Mat frameImgs_[2];
    cv::Mat eventImgs_[2];
    size_t frameIndices_[2]{0,0};
    size_t eventIndices_[2]{0,0};
};

static fs::path makePlaybackDir() {
    auto dir = fs::temp_directory_path()/fs::path("ebv_buf_playback_"+std::to_string(::getpid())+"_"+std::to_string(rand()));
    fs::create_directories(dir/"frame_cam0");
    cv::imwrite((dir/"frame_cam0"/"frame_0.jpg").string(), cv::Mat(5,5,CV_8UC3, cv::Scalar(0,0,255)));
    return dir;
}

TEST(RecordingBufferPlayback, BasicFrameRetrieval) {
    int argc=0; char** argv=nullptr; if(!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    RecordingLoader loader; RecordingBuffer buffer;
    auto dir = makePlaybackDir();
    loader.loadRecording(dir.string());
    // Wait until ready
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!loader.isDataReady() && std::chrono::steady_clock::now()<deadline) { QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    ASSERT_TRUE(loader.isDataReady());
    buffer.setPlaybackMode(&loader);
    buffer.setCurrentFrameIndex(0);
    auto frame = buffer.getFrameCameraFrame(0,0);
    EXPECT_FALSE(frame.empty());
    EXPECT_EQ(buffer.getTotalFrames(), loader.getData().totalFrames);
}

TEST(RecordingBufferLive, LiveDataAccumulation) {
    int argc=0; char** argv=nullptr; if(!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    auto manager = std::make_unique<FakeLiveRecordingManager>();
    RecordingBuffer buffer;
    buffer.setLiveMode(static_cast<void*>(manager.get()));
    // Feed some frames
    for(int i=0;i<5;++i){
        manager->pushFrame(0, cv::Mat(10,10,CV_8UC3, cv::Scalar(i,0,0)));
        manager->pushEvent(0, cv::Mat(10,10,CV_8UC3, cv::Scalar(0,i,0)));
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); // allow worker to poll
    }
    // Allow processing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto live = buffer.getLatestLiveData();
    EXPECT_TRUE(live.isValid);
    EXPECT_GE(buffer.getLiveFrameCount(), 1u);
    EXPECT_FALSE(live.frameData[0].image.empty());
}
