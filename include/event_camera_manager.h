#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <opencv2/opencv.hpp>
#include <metavision/sdk/stream/camera.h>
#include <metavision/hal/facilities/i_camera_synchronization.h>
#include <metavision/hal/facilities/i_ll_biases.h>
#include <metavision/hal/device/device_discovery.h>
#include <metavision/sdk/base/events/event_cd.h>

struct BiasLimits {
    int min_value;
    int max_value;
};

struct EventFrameData {
    cv::Mat frame;  // Use cv::Mat instead of QImage to avoid Qt dependency
    int cameraId;
    size_t frameIndex;
    std::chrono::steady_clock::time_point timestamp;
    bool isValid{false};
};

class EventCameraManager {
public:
    using BiasConfig = std::unordered_map<std::string, int>; 
    struct CameraConfig {
        std::string serial;
        std::unordered_map<std::string,int> biases; // biases matched to this serial
    };

    EventCameraManager();
    ~EventCameraManager();

    // NOTE: This code is in principle compatible with an arbitrary number of event cameras,
    // but it has only been tested for two cameras
    void openAndSetupDevices(const std::vector<CameraConfig>& cameraConfigs = {});
    void startRecording(const std::string& outputPath, const std::string& fileFormat = "hdf5");
    void stopRecording();
    void closeDevices(); // Explicitly close all cameras and release resources
    
    // Live data access for recording buffer
    bool startLiveStreaming();
    void stopLiveStreaming();
    bool getLatestEventFrame(int cameraId, cv::Mat& eventFrame, size_t& frameIndex);

private:
    void setupDevice(std::unique_ptr<Metavision::Camera>& camera, bool isMaster, const BiasConfig& biases);
    void setBiases(std::unique_ptr<Metavision::Camera>& camera, const BiasConfig& biases);
    std::vector<std::string> discoverAvailableCameras();
    std::vector<CameraConfig> createAutoDiscoveryConfigs();
    void validateCameraConfigs(const std::vector<CameraConfig>& configs);
    BiasConfig clipBiasValues(const BiasConfig& biases);
    bool validateBiasLimits(const std::string& biasName, int value);
    
    static const std::unordered_map<std::string, BiasLimits> DEFAULT_BIAS_LIMITS;
    static const std::unordered_map<std::string, int> DEFAULT_BIASES;

    std::vector<std::unique_ptr<Metavision::Camera>> m_cameras; // index 0 master, rest slaves
    std::atomic<bool> m_recording;
    std::string m_outputPath;
    
    // Live streaming support
    std::atomic<bool> m_liveStreaming{false};
    std::vector<std::thread> m_eventStreamingThreads;
    std::vector<std::queue<EventFrameData>> m_liveEventBuffers;
    std::vector<std::unique_ptr<std::mutex>> m_eventBufferMutexes;
    std::vector<size_t> m_eventFrameCounters;
    
    // Event accumulation parameters
    static constexpr size_t MAX_EVENT_BUFFER_SIZE = 100;
    static constexpr double EVENT_FRAME_RATE = 30.0; // Generate frames at 30 FPS
    static constexpr int EVENT_FRAME_WIDTH = 640;
    static constexpr int EVENT_FRAME_HEIGHT = 480;
    
    // Live streaming methods
    void eventStreamingWorker(int cameraId);
    cv::Mat generateEventFrame(const std::vector<Metavision::EventCD>& events, int width, int height);
};
