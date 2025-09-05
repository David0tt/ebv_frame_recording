#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <functional>
#include "event_camera_manager.h"
#include "frame_camera_manager.h"

class RecordingManager {
public:
    // Abstract interfaces to enable mocking in tests (Phase 3)
    struct IFrameCameraManager {
        virtual ~IFrameCameraManager() = default;
        virtual void openAndSetupDevices() = 0;
        virtual void startRecording(const std::string& outputPath) = 0;
        virtual void stopRecording() = 0;
        virtual void closeDevices() = 0;
        virtual bool getLatestFrame(int deviceId, FrameData& frameData) = 0;
    };
    struct IEventCameraManager {
    using BiasConfig = std::unordered_map<std::string,int>;
    using CameraConfig = EventCameraManager::CameraConfig; // reuse concrete struct for simplicity
        virtual ~IEventCameraManager() = default;
        virtual void openAndSetupDevices(const std::vector<CameraConfig>& cameraConfigs) = 0;
        virtual void startRecording(const std::string& outputPath, const std::string& fileFormat) = 0;
        virtual void stopRecording() = 0;
        virtual void closeDevices() = 0;
        virtual bool startLiveStreaming() = 0;
        virtual void stopLiveStreaming() = 0;
        virtual bool getLatestEventFrame(int cameraId, cv::Mat& eventFrame, size_t& frameIndex) = 0;
    };
    // Configuration structure for recording
    struct RecordingConfig {
        std::vector<std::string> eventCameraSerials;
        std::unordered_map<std::string, std::vector<int>> biases;
        std::string eventFileFormat = "hdf5";  // "raw" or "hdf5"
        std::string outputPrefix = "";
        int recordingLengthSeconds = -1;  // -1 for indefinite
    };

    // Status callback function type
    using StatusCallback = std::function<void(const std::string& message)>;

    RecordingManager();
    // Dependency injection constructor for tests
    RecordingManager(std::unique_ptr<IFrameCameraManager> frameMgr,
                     std::unique_ptr<IEventCameraManager> eventMgr);
    ~RecordingManager();

    // Configuration interface - must be called before recording
    bool configure(const RecordingConfig& config);
    bool isConfigured() const { return m_configured; }
    
    // Main recording interface
    bool startRecording(const std::string& outputDirectory);
    void stopRecording();
    void closeDevices(); // Close and release all camera resources
    
    // Legacy interface for backward compatibility
    bool startRecording(const RecordingConfig& config);
    bool startRecording(const std::string& outputDirectory, const RecordingConfig& config);
    
    // Status and information
    bool isRecording() const { return m_recording; }
    std::string getCurrentOutputDirectory() const { return m_currentOutputDir; }
    std::chrono::steady_clock::time_point getRecordingStartTime() const { return m_recordingStartTime; }
    double getRecordingDurationSeconds() const;

    // ---- Test helpers (Phase 2) ----
    // Exposes private timestamped directory generation for unit tests without
    // changing production call sites. Safe: pure function of current time + prefix.
    std::string testGenerateOutputDirectory(const std::string& prefix) const { return generateOutputDirectory(prefix); }
    
    // Callbacks for status updates
    void setStatusCallback(StatusCallback callback) { m_statusCallback = callback; }
    
    // Graceful shutdown support
    void setShutdownFlag(std::atomic<bool>* shutdownFlag) { m_shutdownFlag = shutdownFlag; }
    
    // Live data access for recording buffer
    bool getLiveFrameData(int cameraId, cv::Mat& frame, size_t& frameIndex);
    bool getLiveEventData(int cameraId, cv::Mat& eventFrame, size_t& frameIndex);

private:
    std::string generateOutputDirectory(const std::string& prefix = "") const;
    std::vector<EventCameraManager::CameraConfig> createEventCameraConfigs(const RecordingConfig& config) const;
    void validateConfig(const RecordingConfig& config) const;
    void notifyStatus(const std::string& message) const;
    
    // Camera managers
    // Use abstract pointers to allow substitution with mocks
    std::unique_ptr<IFrameCameraManager> m_frameCameraManager;
    std::unique_ptr<IEventCameraManager> m_eventCameraManager;
    
    // Recording state
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_configured{false};
    std::string m_currentOutputDir;
    std::chrono::steady_clock::time_point m_recordingStartTime;
    RecordingConfig m_currentConfig;
    
    // Callbacks and external control
    StatusCallback m_statusCallback;
    std::atomic<bool>* m_shutdownFlag{nullptr};
    
    // Default bias values
    static const std::unordered_map<std::string, int> DEFAULT_BIASES;
};
