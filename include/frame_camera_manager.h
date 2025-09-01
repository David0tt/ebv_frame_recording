#pragma once

#include <peak/peak.hpp>
// #include <peak_ipl/peak_ipl.hpp>
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <opencv2/opencv.hpp>

class FrameCameraManager {
public:
    FrameCameraManager();
    ~FrameCameraManager();

    void openAndSetupDevices();
    void startRecording(const std::string& outputPath);
    void stopRecording();

private:
    void setupDevice(std::shared_ptr<peak::core::Device> device);
    void acquisitionWorker(int deviceId, const std::string& outputPath);

    std::vector<std::shared_ptr<peak::core::Device>> m_devices;
    std::vector<std::shared_ptr<peak::core::DataStream>> m_dataStreams;
    std::vector<std::thread> m_acquisitionThreads;
    std::atomic<bool> m_acquiring;

    std::vector<std::vector<cv::Mat>> m_frames;
};

