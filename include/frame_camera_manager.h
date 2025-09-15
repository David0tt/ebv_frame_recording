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
#include <optional>

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
    // Start acquisition and writing to disk
    void startRecording(const std::string& outputPath);
    // Stop acquisition and disk writing
    void stopRecording();
    
    // Live preview (acquisition without disk writing)
    void startPreview();
    void stopPreview();
    
    // Seamless transition controls when preview is already running
    void startRecordingToPath(const std::string& outputPath); // start only disk writer
    void stopRecordingOnly(); // stop only disk writer, keep preview acquisition running
    void closeDevices(); // Close and release all camera resources
    
    // Live data access for recording buffer
    bool getLatestFrame(int deviceId, FrameData& frameData);

private:
    void setupDevice(std::shared_ptr<peak::core::Device> device);
    void acquisitionWorker(int deviceId);
    void diskWriterWorker(const std::string& outputPath);
    void startAcquisition();
    void stopAcquisition();

    std::vector<std::shared_ptr<peak::core::Device>> m_devices;
    std::vector<std::shared_ptr<peak::core::DataStream>> m_dataStreams;
    std::vector<std::thread> m_acquisitionThreads;
    std::thread m_diskWriterThread;
    std::atomic<bool> m_acquiring;
    std::atomic<bool> m_writingToDisk{false};

    // Frame buffer queue for non-blocking capture
    std::queue<FrameData> m_frameQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    static constexpr size_t MAX_QUEUE_SIZE = 1000; // Adjust based on memory constraints

    // Latest frame per device for live preview access (decoupled from writer queue)
    std::vector<FrameData> m_latestFrames;
    std::mutex m_latestMutex;
};

