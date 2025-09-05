#include <gtest/gtest.h>
#include <QCoreApplication>
#include <opencv2/opencv.hpp>
#include "recording_buffer.h"
#include "recording_manager.h"
#include <thread>

// Reuse FakeLiveRecordingManager from existing test by redefining minimal stub (not exported)
class FakeLiveRecordingManager2 : public RecordingManager {
public:
    FakeLiveRecordingManager2(): RecordingManager(nullptr,nullptr) {}
    void pushFrame(int cam, const cv::Mat &img) { std::lock_guard<std::mutex> l(mu); frameImgs[cam]=img.clone(); frameIdx[cam]++; }
    void pushEvent(int cam, const cv::Mat &img) { std::lock_guard<std::mutex> l(mu); eventImgs[cam]=img.clone(); eventIdx[cam]++; }
    bool getLiveFrameData(int cameraId, cv::Mat &frame, size_t &frameIndex){ std::lock_guard<std::mutex> l(mu); if(frameImgs[cameraId].empty()) return false; frame=frameImgs[cameraId].clone(); frameIndex=frameIdx[cameraId]; return true; }
    bool getLiveEventData(int cameraId, cv::Mat &f, size_t &i){ std::lock_guard<std::mutex> l(mu); if(eventImgs[cameraId].empty()) return false; f=eventImgs[cameraId].clone(); i=eventIdx[cameraId]; return true; }
    bool isRecording() const { return true; }
private:
    mutable std::mutex mu; cv::Mat frameImgs[2]; cv::Mat eventImgs[2]; size_t frameIdx[2]{0,0}; size_t eventIdx[2]{0,0};
};

TEST(RecordingBufferExtended, StopClearsState) {
    int argc=0; char** argv=nullptr; if(!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    RecordingBuffer buf; buf.stop(); // idempotent
    EXPECT_FALSE(buf.isActive());
}

TEST(RecordingBufferExtended, LiveBufferHealthAndFps) {
    int argc=0; char** argv=nullptr; if(!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    auto mgr = std::make_unique<FakeLiveRecordingManager2>();
    RecordingBuffer buf; buf.setLiveMode(static_cast<void*>(mgr.get()));
    // Feed frames more than TARGET_BUFFER_SIZE (100) quickly
    for(int i=0;i<120;++i){
        mgr->pushFrame(0, cv::Mat(5,5,CV_8UC3, cv::Scalar(i,0,0)));
        mgr->pushEvent(0, cv::Mat(5,5,CV_8UC3, cv::Scalar(0,i,0)));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_GE(buf.getBufferSize(), 50u); // some accumulation
    // Wait enough for FPS update ( >1s )
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    double fps = buf.getCurrentFPS();
    EXPECT_GT(fps, 0.0);
}
