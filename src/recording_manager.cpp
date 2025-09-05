#include "recording_manager.h"
#include <iostream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

// Default bias values
const std::unordered_map<std::string, int> RecordingManager::DEFAULT_BIASES = {
    {"bias_diff_on", 0}, 
    {"bias_diff_off", 0}, 
    {"bias_fo", 0}, 
    {"bias_hpf", 0}, 
    {"bias_refr", 0}
};

RecordingManager::RecordingManager() 
    : m_frameCameraManager(std::make_unique<FrameCameraManager>())
    , m_eventCameraManager(std::make_unique<EventCameraManager>())
{
}

RecordingManager::~RecordingManager() {
    if (m_recording) {
        stopRecording();
    }
    // Close devices to release resources
    closeDevices();
}

bool RecordingManager::configure(const RecordingConfig& config) {
    if (m_recording) {
        notifyStatus("Error: Cannot reconfigure while recording is in progress");
        return false;
    }

    try {
        validateConfig(config);
        
        // Close any previously opened devices
        closeDevices();
        
        // Store configuration
        m_currentConfig = config;
        
        notifyStatus("Configuring cameras...");
        
        // Open and setup frame cameras
        notifyStatus("Setting up frame cameras...");
        m_frameCameraManager->openAndSetupDevices();
        
        // Open and setup event cameras
        notifyStatus("Setting up event cameras...");
        auto eventConfigs = createEventCameraConfigs(config);
        m_eventCameraManager->openAndSetupDevices(eventConfigs);
        
        m_configured = true;
        notifyStatus("Camera configuration completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        notifyStatus("Error configuring cameras: " + std::string(e.what()));
        m_configured = false;
        return false;
    }
}

bool RecordingManager::startRecording(const std::string& outputDirectory) {
    if (m_recording) {
        notifyStatus("Error: Recording is already in progress");
        return false;
    }
    
    if (!m_configured) {
        notifyStatus("Error: Cameras must be configured before starting recording");
        return false;
    }

    try {
        // Create output directory
        std::filesystem::create_directories(outputDirectory);
        m_currentOutputDir = outputDirectory;
        
        notifyStatus("Starting recording to: " + outputDirectory);
        m_recordingStartTime = std::chrono::steady_clock::now();
        
        // Start recording on both managers (devices are already configured)
        notifyStatus("Starting event camera recording...");
        m_eventCameraManager->startRecording(outputDirectory, m_currentConfig.eventFileFormat);
        
        notifyStatus("Starting frame camera recording...");
        m_frameCameraManager->startRecording(outputDirectory);
        
        // Start live streaming for both cameras
        if (!m_eventCameraManager->startLiveStreaming()) {
            notifyStatus("Warning: Failed to start event camera live streaming");
        }
        
        m_recording = true;
        
        if (m_currentConfig.recordingLengthSeconds > 0) {
            notifyStatus("Recording for " + std::to_string(m_currentConfig.recordingLengthSeconds) + " seconds...");
        } else {
            notifyStatus("Recording indefinitely. Call stopRecording() to stop.");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        notifyStatus("Error starting recording: " + std::string(e.what()));
        return false;
    }
}

// Legacy interface for backward compatibility
bool RecordingManager::startRecording(const RecordingConfig& config) {
    std::string outputDir = generateOutputDirectory(config.outputPrefix);
    return startRecording(outputDir, config);
}

bool RecordingManager::startRecording(const std::string& outputDirectory, const RecordingConfig& config) {
    // Configure first, then start recording
    if (!configure(config)) {
        return false;
    }
    return startRecording(outputDirectory);
}

void RecordingManager::stopRecording() {
    if (!m_recording) {
        return;
    }
    
    notifyStatus("Stopping recording...");
    
    try {
        // Stop frame camera recording first (typically faster to stop)
        notifyStatus("Stopping frame camera recording...");
        m_frameCameraManager->stopRecording();
        
        // Stop event camera recording and ensure flushing
        notifyStatus("Stopping event camera recording and flushing data...");
        m_eventCameraManager->stopRecording();
        
        // Stop live streaming
        m_eventCameraManager->stopLiveStreaming();
        
        // Give extra time for buffers to flush to disk
        notifyStatus("Waiting for data flush to complete...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        m_recording = false;
        
        auto duration = getRecordingDurationSeconds();
        notifyStatus("Recording completed successfully! Duration: " + 
                    std::to_string(duration) + " seconds");
                    
        notifyStatus("All recording data has been flushed to disk");
                    
    } catch (const std::exception& e) {
        notifyStatus("Error stopping recording: " + std::string(e.what()));
        m_recording = false;
    }
}

void RecordingManager::closeDevices() {
    try {
        notifyStatus("Closing and releasing camera resources...");
        
        // Stop recording if still active
        if (m_recording) {
            stopRecording();
        }
        
        // Close event cameras
        m_eventCameraManager->closeDevices();
        
        // Close frame cameras  
        m_frameCameraManager->closeDevices();
        
        m_configured = false;
        notifyStatus("All camera resources released successfully");
    } catch (const std::exception& e) {
        notifyStatus("Error closing devices: " + std::string(e.what()));
    }
}

double RecordingManager::getRecordingDurationSeconds() const {
    if (!m_recording && m_recordingStartTime == std::chrono::steady_clock::time_point{}) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_recordingStartTime);
    return duration.count() / 1000.0;
}

std::string RecordingManager::generateOutputDirectory(const std::string& prefix) const {
    const auto now = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    const auto tm = *std::localtime(&time_t);
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    
    std::string outputDir = "./recording/";
    if (!prefix.empty()) {
        outputDir += prefix + "_";
    }
    outputDir += std::string(timestamp);
    
    return outputDir;
}

std::vector<EventCameraManager::CameraConfig> RecordingManager::createEventCameraConfigs(const RecordingConfig& config) const {
    std::vector<EventCameraManager::CameraConfig> cameraConfigs;
    
    if (config.eventCameraSerials.empty()) {
        // Return empty vector for auto-discovery
        notifyStatus("Using event camera auto-discovery (no explicit serials provided)");
        return cameraConfigs;
    }
    
    // Validate that if any bias is provided, its vector has the same size as the serials vector
    for (const auto& [key, val] : config.biases) {
        if (!val.empty() && val.size() != config.eventCameraSerials.size()) {
            throw std::runtime_error("Number of bias values for " + key + " must match number of serials.");
        }
    }
    
    // Create configurations for each camera
    for (size_t i = 0; i < config.eventCameraSerials.size(); ++i) {
        EventCameraManager::BiasConfig camera_biases;
        
        for (const auto& [key, default_val] : DEFAULT_BIASES) {
            if (config.biases.count(key) && i < config.biases.at(key).size()) {
                camera_biases[key] = config.biases.at(key)[i];
            } else {
                camera_biases[key] = default_val;
            }
        }
        
        cameraConfigs.push_back({config.eventCameraSerials[i], camera_biases});
    }
    
    return cameraConfigs;
}

void RecordingManager::validateConfig(const RecordingConfig& config) const {
    // Validate event file format
    if (config.eventFileFormat != "raw" && config.eventFileFormat != "hdf5") {
        throw std::runtime_error("Invalid event file format '" + config.eventFileFormat + 
                                "'. Supported formats are 'raw' and 'hdf5'.");
    }
    
    // Additional validation can be added here as needed
}

void RecordingManager::notifyStatus(const std::string& message) const {
    if (m_statusCallback) {
        m_statusCallback(message);
    } else {
        // Default behavior: print to console
        std::cout << message << std::endl;
    }
}

bool RecordingManager::getLiveFrameData(int cameraId, cv::Mat& frame, size_t& frameIndex) {
    if (!m_recording || !m_frameCameraManager) {
        return false;
    }
    
    FrameData frameData;
    if (m_frameCameraManager->getLatestFrame(cameraId, frameData)) {
        frame = frameData.image;
        frameIndex = frameData.frameIndex;
        return true;
    }
    
    return false;
}

bool RecordingManager::getLiveEventData(int cameraId, cv::Mat& eventFrame, size_t& frameIndex) {
    if (!m_recording || !m_eventCameraManager) {
        return false;
    }
    
    return m_eventCameraManager->getLatestEventFrame(cameraId, eventFrame, frameIndex);
}
