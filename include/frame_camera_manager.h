#pragma once

#include <peak/peak.hpp>
// #include <peak_ipl/peak_ipl.hpp>
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <opencv2/opencv.hpp>

struct FrameData {
    cv::Mat image;
    int deviceId;
    int frameIndex;
    std::chrono::steady_clock::time_point timestamp;
};

class FrameCameraManager {
public:
    FrameCameraManager();
    ~FrameCameraManager();

    void openAndSetupDevices();
    void startRecording(const std::string& outputPath);
    void stopRecording();

private:
    void setupDevice(std::shared_ptr<peak::core::Device> device);
    void acquisitionWorker(int deviceId);
    void diskWriterWorker(const std::string& outputPath);

    std::vector<std::shared_ptr<peak::core::Device>> m_devices;
    std::vector<std::shared_ptr<peak::core::DataStream>> m_dataStreams;
    std::vector<std::thread> m_acquisitionThreads;
    std::thread m_diskWriterThread;
    std::atomic<bool> m_acquiring;

    // Frame buffer queue for non-blocking capture
    std::queue<FrameData> m_frameQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    static constexpr size_t MAX_QUEUE_SIZE = 1000; // Adjust based on memory constraints

    std::vector<std::vector<cv::Mat>> m_frames;
};

