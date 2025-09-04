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
    ~RecordingManager();

    // Main recording interface
    bool startRecording(const RecordingConfig& config);
    bool startRecording(const std::string& outputDirectory, const RecordingConfig& config);
    void stopRecording();
    
    // Status and information
    bool isRecording() const { return m_recording; }
    std::string getCurrentOutputDirectory() const { return m_currentOutputDir; }
    std::chrono::steady_clock::time_point getRecordingStartTime() const { return m_recordingStartTime; }
    double getRecordingDurationSeconds() const;
    
    // Callbacks for status updates
    void setStatusCallback(StatusCallback callback) { m_statusCallback = callback; }
    
    // Graceful shutdown support
    void setShutdownFlag(std::atomic<bool>* shutdownFlag) { m_shutdownFlag = shutdownFlag; }

private:
    std::string generateOutputDirectory(const std::string& prefix = "") const;
    std::vector<EventCameraManager::CameraConfig> createEventCameraConfigs(const RecordingConfig& config) const;
    void validateConfig(const RecordingConfig& config) const;
    void notifyStatus(const std::string& message) const;
    
    // Camera managers
    std::unique_ptr<FrameCameraManager> m_frameCameraManager;
    std::unique_ptr<EventCameraManager> m_eventCameraManager;
    
    // Recording state
    std::atomic<bool> m_recording{false};
    std::string m_currentOutputDir;
    std::chrono::steady_clock::time_point m_recordingStartTime;
    RecordingConfig m_currentConfig;
    
    // Callbacks and external control
    StatusCallback m_statusCallback;
    std::atomic<bool>* m_shutdownFlag{nullptr};
    
    // Default bias values
    static const std::unordered_map<std::string, int> DEFAULT_BIASES;
};
