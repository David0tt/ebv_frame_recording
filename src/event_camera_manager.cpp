#include "event_camera_manager.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/hal/device/device_discovery.h>
#include <opencv2/opencv.hpp>

// Static bias maps moved to header as inline definitions for testability.


EventCameraManager::EventCameraManager() : m_recording(false) {
    // Reserve space for typical setup, but can grow as needed
    m_cameras.reserve(2);
}

EventCameraManager::~EventCameraManager() {
    stopLiveStreaming();
    stopRecording();
}

void EventCameraManager::openAndSetupDevices(const std::vector<CameraConfig>& cameraConfigs) {
    try {
        m_cameras.clear();

        const bool auto_discovery = cameraConfigs.empty();
        std::vector<CameraConfig> configs = auto_discovery ? createAutoDiscoveryConfigs() : cameraConfigs;
        
        if (!auto_discovery) {
            validateCameraConfigs(configs);
        }

        // Open and configure cameras
        for (size_t i = 0; i < configs.size(); ++i) {
            const auto& [serial, biases] = configs[i];
            const bool isMaster = (i == 0);
            
            std::cout << "Opening " << (isMaster ? "master" : "slave") << " camera with serial: " << serial << std::endl;
            auto camera = std::make_unique<Metavision::Camera>(Metavision::Camera::from_serial(serial));
            setupDevice(camera, isMaster, biases);
            m_cameras.push_back(std::move(camera));
        }
        
        // Initialize live streaming structures
        m_liveEventBuffers.resize(m_cameras.size());
        m_eventBufferMutexes.resize(m_cameras.size());
        m_eventFrameCounters.resize(m_cameras.size());
    m_startedForStreaming.assign(m_cameras.size(), false);
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            m_eventBufferMutexes[i] = std::make_unique<std::mutex>();
            m_eventFrameCounters[i] = 0;
        }
        
        std::cout << "Successfully opened and configured " << m_cameras.size() 
                  << " event cameras (1 master, " << (m_cameras.size() - 1) << " slaves)" << std::endl;

    } catch (const Metavision::CameraException& e) {
        throw std::runtime_error("Failed to open event cameras: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to configure event cameras: " + std::string(e.what()));
    }
}

void EventCameraManager::setupDevice(std::unique_ptr<Metavision::Camera>& camera, bool isMaster, const BiasConfig& biases) {
    auto& sync = camera->get_facility<Metavision::I_CameraSynchronization>();
    
    const bool success = isMaster ? sync.set_mode_master() : sync.set_mode_slave();
    if (!success) {
        throw std::runtime_error("Failed to set " + std::string(isMaster ? "master" : "slave") + " camera synchronization mode");
    }
    
    const std::string cameraType = isMaster ? "Master" : "Slave";
    std::cout << "Camera set to " << (isMaster ? "master" : "slave") << " mode" << std::endl;
    std::cout << "\nSetting biases for " << cameraType << " camera:" << std::endl;
    setBiases(camera, biases);
}

std::vector<std::string> EventCameraManager::discoverAvailableCameras() {
    std::vector<std::string> serialNumbers;
    
    auto device_serials = Metavision::DeviceDiscovery::list();
    
    std::cout << "Discovering available event cameras..." << std::endl;
    std::cout << "DeviceDiscovery::list() returned " << device_serials.size() << " configurations" << std::endl;
    
    if (device_serials.empty()) {
        std::cout << "No event cameras detected via DeviceDiscovery" << std::endl;
        return serialNumbers;
    }

    size_t idx = 0;
    for (const auto& serial : device_serials) {
        std::cout << "Device config " << idx++ << ": " << serial << std::endl;
        serialNumbers.push_back(serial);
    }
    
    return serialNumbers;
}

std::vector<EventCameraManager::CameraConfig> EventCameraManager::createAutoDiscoveryConfigs() {
    auto serials = discoverAvailableCameras();
    if (serials.empty()) {
        throw std::runtime_error("No event cameras found for auto-discovery");
    }
    
    std::sort(serials.begin(), serials.end());
    std::cout << "Auto-discovered " << serials.size() << " event cameras" << std::endl;
    std::cout << "Master camera (lowest serial): " << serials[0] << std::endl;
    std::cout << "WARNING: No serial numbers for the event cameras with corresponding biases were provided! "
              << "Therefore auto device-discovery and default biases are used. Each event camera requires "
              << "distinct manual selection of its biases, so this setup is discouraged!" << std::endl;
    
    std::vector<CameraConfig> configs;
    for (const auto& serial : serials) {
        configs.push_back(CameraConfig{serial, DEFAULT_BIASES});
    }
    return configs;
}

void EventCameraManager::validateCameraConfigs(const std::vector<CameraConfig>& configs) {
    for (const auto& config : configs) {
        if (config.biases.empty()) {
            throw std::runtime_error("Bias configuration missing for camera serial: " + config.serial);
        }
    }
}

void EventCameraManager::setBiases(std::unique_ptr<Metavision::Camera>& camera, const BiasConfig& biases) {
    if (!camera) {
        throw std::runtime_error("Camera must be opened before setting biases");
    }

    auto& bias_facility = camera->get_facility<Metavision::I_LL_Biases>();
    const BiasConfig clippedBiases = clipBiasValues(biases);

    for (const auto& [bias_name, bias_value] : clippedBiases) {
        if (!validateBiasLimits(bias_name, bias_value)) {
            throw std::runtime_error("Invalid bias value for " + bias_name);
        }
        
        if (!bias_facility.set(bias_name, bias_value)) {
            std::cerr << "Warning: Failed to set bias " << bias_name << " to " << bias_value << std::endl;
        } else {
            std::cout << "  " << bias_name << " = " << bias_value << std::endl;
        }
    }
}

void EventCameraManager::startRecording(const std::string& outputPath, const std::string& fileFormat) {
    if (m_cameras.empty()) {
        throw std::runtime_error("Cameras must be opened before starting recording");
    }

    // Validate file format
    if (fileFormat != "raw" && fileFormat != "hdf5") {
        throw std::runtime_error("Invalid file format: " + fileFormat + ". Supported formats are 'raw' and 'hdf5'");
    }

    m_outputPath = outputPath;
    std::filesystem::create_directories(outputPath);

    try {
        // Start recording and cameras
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            const std::string filename = outputPath + "/ebv_cam_" + std::to_string(i) + "." + fileFormat;
            const std::string cameraType = (i == 0) ? "master" : "slave";
            
            if (!m_cameras[i]->start_recording(filename)) {
                throw std::runtime_error("Failed to start recording for camera " + std::to_string(i));
            }
            std::cout << "Started recording " << cameraType << " camera " << i << " to: " << filename << std::endl;
            
            // If camera is already running (e.g., due to live preview), don't try to start again
            if (!m_cameras[i]->is_running()) {
                if (!m_cameras[i]->start()) {
                    throw std::runtime_error("Failed to start camera " + std::to_string(i));
                }
                const std::string capitalizedType = (i == 0) ? "Master" : "Slave";
                std::cout << capitalizedType << " camera " << i << " started" << std::endl;
            } else {
                std::cout << "Camera " << i << " already running; continuing with recording" << std::endl;
            }
        }

        m_recording = true;
        std::cout << "Event camera recording started successfully for " << m_cameras.size() << " cameras in " << fileFormat << " format" << std::endl;

    } catch (const Metavision::CameraException& e) {
        throw std::runtime_error("Failed to start recording: " + std::string(e.what()));
    }
}

void EventCameraManager::stopRecording() {
    if (!m_recording || m_cameras.empty()) {
        return;
    }
    
    try {
        // If live streaming is active, briefly pause to allow writer to flush events
        bool resumeStreaming = false;
        if (m_liveStreaming) {
            stopLiveStreaming();
            resumeStreaming = true;
        }
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            if (m_cameras[i]) {
                m_cameras[i]->stop_recording();
                // If live streaming is still active (preview mode), keep camera running
                if (!m_liveStreaming) {
                    m_cameras[i]->stop();
                    const std::string cameraType = (i == 0) ? "master" : "slave";
                    std::cout << "Stopped " << cameraType << " camera " << i << std::endl;
                } else {
                    std::cout << "Stopped recording on camera " << i << " (kept running for preview)" << std::endl;
                }
            }
        }
        m_recording = false;
        std::cout << "Event camera recording stopped successfully for " << m_cameras.size() << " cameras" << std::endl;
        // Resume live streaming if it was active before
        if (resumeStreaming) {
            startLiveStreaming();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error stopping cameras: " << e.what() << std::endl;
    }
}

void EventCameraManager::closeDevices() {
    try {
        // Stop recording if still active
        if (m_recording) {
            stopRecording();
        }
        
        // Close and release all cameras
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            if (m_cameras[i]) {
                const std::string cameraType = (i == 0) ? "master" : "slave";
                std::cout << "Closing " << cameraType << " camera " << i << std::endl;
                // Camera destructor will handle proper cleanup
                m_cameras[i].reset();
            }
        }
        
        m_cameras.clear();
        std::cout << "All event cameras closed and resources released" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error closing cameras: " << e.what() << std::endl;
    }
}

EventCameraManager::BiasConfig EventCameraManager::clipBiasValues(const BiasConfig& biases) {
    BiasConfig clippedBiases = biases;
    
    for (auto& [bias_name, bias_value] : clippedBiases) {
        const auto it = DEFAULT_BIAS_LIMITS.find(bias_name);
        if (it == DEFAULT_BIAS_LIMITS.end()) {
            throw std::runtime_error("Bias name '" + bias_name + "' not found in DEFAULT_BIAS_LIMITS");
        }
        
        const int original_value = bias_value;
        bias_value = std::clamp(bias_value, it->second.min_value, it->second.max_value);
        
        if (bias_value != original_value) {
            std::cerr << "Warning: Bias " << bias_name << " value " << original_value
                      << " was clipped to " << bias_value << " (limits: ["
                      << it->second.min_value << ", " << it->second.max_value << "])" << std::endl;
        }
    }
    
    return clippedBiases;
}

bool EventCameraManager::validateBiasLimits(const std::string& biasName, int value) {
    const auto it = DEFAULT_BIAS_LIMITS.find(biasName);
    if (it == DEFAULT_BIAS_LIMITS.end()) {
        std::cerr << "Warning: Unknown bias name '" << biasName << "'; skipping validation." << std::endl;
        return true; // Unknown bias names are allowed to pass through
    }
    
    const bool isValid = (value >= it->second.min_value && value <= it->second.max_value);
    if (!isValid) {
        std::cerr << "Warning: Bias " << biasName << " value " << value 
                 << " is outside limits [" << it->second.min_value 
                 << ", " << it->second.max_value << "]" << std::endl;
    }
    return isValid;
}

bool EventCameraManager::startLiveStreaming() {
    if (m_cameras.empty()) {
        std::cerr << "Error: No cameras opened for live streaming" << std::endl;
        return false;
    }
    
    if (m_liveStreaming) {
        return true; // Already streaming
    }
    
    try {
        // Ensure cameras are running to deliver events
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            if (!m_recording && m_cameras[i]) {
                if (!m_cameras[i]->is_running()) {
                    if (!m_cameras[i]->start()) {
                        throw std::runtime_error("Failed to start camera for live streaming: " + std::to_string(i));
                    }
                    m_startedForStreaming[i] = true;
                }
            }
        }
        // Initialize buffers and counters for each camera
        // Resize and reset tracking vectors to match number of cameras
        m_liveEventBuffers.resize(m_cameras.size());
        // Clear any lingering data in queues (defensive)
        for (auto &q : m_liveEventBuffers) {
            while (!q.empty()) q.pop();
        }
        m_eventBufferMutexes.resize(m_cameras.size());
        for (size_t i = 0; i < m_eventBufferMutexes.size(); ++i) {
            if (!m_eventBufferMutexes[i]) {
                m_eventBufferMutexes[i] = std::make_unique<std::mutex>();
            }
        }
        m_eventFrameCounters.resize(m_cameras.size());
        for (size_t i = 0; i < m_eventFrameCounters.size(); ++i) {
            m_eventFrameCounters[i] = 0;
        }
        // Ensure started-for-streaming tracking matches camera count
        if (m_startedForStreaming.size() != m_cameras.size()) {
            m_startedForStreaming.assign(m_cameras.size(), false);
        }
        
        // Start streaming threads for each camera
        m_liveStreaming = true;
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            m_eventStreamingThreads.emplace_back(&EventCameraManager::eventStreamingWorker, this, static_cast<int>(i));
        }
        
        std::cout << "Started live streaming for " << m_cameras.size() << " event cameras" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error starting live streaming: " << e.what() << std::endl;
        m_liveStreaming = false;
        return false;
    }
}

void EventCameraManager::stopLiveStreaming() {
    if (!m_liveStreaming) {
        return;
    }
    
    m_liveStreaming = false;
    
    // Wait for all streaming threads to finish
    for (auto& thread : m_eventStreamingThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_eventStreamingThreads.clear();
    
    // Clear buffers
    for (auto& buffer : m_liveEventBuffers) {
        while (!buffer.empty()) {
            buffer.pop();
        }
    }
    m_liveEventBuffers.clear();
    m_eventBufferMutexes.clear();
    m_eventFrameCounters.clear();
    
    // If we started cameras solely for streaming, stop them
    for (size_t i = 0; i < m_cameras.size(); ++i) {
        if (!m_recording && m_startedForStreaming.size() > i && m_startedForStreaming[i]) {
            try {
                m_cameras[i]->stop();
            } catch (...) {}
            m_startedForStreaming[i] = false;
        }
    }
    
        
    std::cout << "Stopped live streaming for event cameras" << std::endl;
}

void EventCameraManager::eventStreamingWorker(int cameraId) {
    if (cameraId >= static_cast<int>(m_cameras.size())) {
        return;
    }
    if (cameraId < 0) {
        return;
    }
    // Validate mutex and counters
    if (m_eventBufferMutexes.size() <= static_cast<size_t>(cameraId) || !m_eventBufferMutexes[cameraId]) {
        std::cerr << "Streaming worker " << cameraId << ": missing mutex; aborting thread" << std::endl;
        return;
    }
    if (m_eventFrameCounters.size() <= static_cast<size_t>(cameraId)) {
        std::cerr << "Streaming worker " << cameraId << ": frame counter out of range; aborting thread" << std::endl;
        return;
    }
    
    std::vector<Metavision::EventCD> eventBuffer;
    eventBuffer.reserve(100000); // Reserve space for events
    
    auto lastFrameTime = std::chrono::steady_clock::now();
    const auto frameInterval = std::chrono::duration<double>(1.0 / EVENT_FRAME_RATE);
    
    // Add callback to collect events
    auto callbackId = m_cameras[cameraId]->cd().add_callback(
        [&eventBuffer](const Metavision::EventCD* begin, const Metavision::EventCD* end) {
            for (auto it = begin; it != end; ++it) {
                eventBuffer.push_back(*it);
            }
        }
    );
    
    try {
        while (m_liveStreaming) {
            auto currentTime = std::chrono::steady_clock::now();
            
            // Check if it's time to generate a new frame
            if (currentTime - lastFrameTime >= frameInterval) {
                if (!eventBuffer.empty()) {
                    // Generate frame from accumulated events
                    cv::Mat frame = generateEventFrame(eventBuffer, EVENT_FRAME_WIDTH, EVENT_FRAME_HEIGHT);
                    
                    // Create frame data
                    EventFrameData frameData;
                    frameData.frame = frame.clone(); // Clone to ensure thread safety
                    frameData.cameraId = cameraId;
                    frameData.frameIndex = m_eventFrameCounters[cameraId]++;
                    frameData.timestamp = currentTime;
                    frameData.isValid = !frame.empty();
                    
                    // Add to buffer
                    if (m_eventBufferMutexes.size() > static_cast<size_t>(cameraId) && m_eventBufferMutexes[cameraId]) {
                        std::lock_guard<std::mutex> lock(*m_eventBufferMutexes[cameraId]);
                        if (m_liveEventBuffers.size() > static_cast<size_t>(cameraId)) {
                            m_liveEventBuffers[cameraId].push(frameData);
                            // Keep buffer size under control
                            while (m_liveEventBuffers[cameraId].size() > MAX_EVENT_BUFFER_SIZE) {
                                m_liveEventBuffers[cameraId].pop();
                            }
                        }
                    }
                    
                    // Clear event buffer for next frame
                    eventBuffer.clear();
                }
                
                lastFrameTime = currentTime;
            }
            
            // Sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in event streaming worker " << cameraId << ": " << e.what() << std::endl;
    }
    
    // Remove callback safely
    try {
        if (cameraId >= 0 && cameraId < static_cast<int>(m_cameras.size()) && m_cameras[cameraId]) {
            m_cameras[cameraId]->cd().remove_callback(callbackId);
        }
    } catch (...) {
        // Swallow any errors on teardown to avoid thread termination issues
    }
}

cv::Mat EventCameraManager::generateEventFrame(const std::vector<Metavision::EventCD>& events, int width, int height) {
    // Create accumulation frame
    cv::Mat frame = cv::Mat::zeros(height, width, CV_8UC3);
    
    // Background color (dark gray)
    frame.setTo(cv::Scalar(64, 64, 64));
    
    // Accumulate events with simple visualization
    for (const auto& event : events) {
        if (event.x >= 0 && event.x < width && event.y >= 0 && event.y < height) {
            cv::Vec3b& pixel = frame.at<cv::Vec3b>(event.y, event.x);
            if (event.p == 1) { // Positive event (white)
                pixel[0] = 255; pixel[1] = 255; pixel[2] = 255;
            } else { // Negative event (blue)  
                pixel[0] = 255; pixel[1] = 0; pixel[2] = 0; // BGR format: Blue=255, Green=0, Red=0
            }
        }
    }
    
    return frame;
}

bool EventCameraManager::getLatestEventFrame(int cameraId, cv::Mat& eventFrame, size_t& frameIndex) {
    if (cameraId < 0 || static_cast<size_t>(cameraId) >= m_cameras.size() || !m_liveStreaming) {
        return false;
    }
    // Validate data structures before dereferencing
    if (m_eventBufferMutexes.size() <= static_cast<size_t>(cameraId) || !m_eventBufferMutexes[cameraId]) {
        return false;
    }
    if (m_liveEventBuffers.size() <= static_cast<size_t>(cameraId)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(*m_eventBufferMutexes[cameraId]);
    if (m_liveEventBuffers[cameraId].empty()) {
        return false;
    }
    
    // Get the most recent frame
    EventFrameData latestFrame = m_liveEventBuffers[cameraId].back();
    eventFrame = latestFrame.frame.clone(); // Clone to ensure thread safety
    frameIndex = latestFrame.frameIndex;
    
    return latestFrame.isValid;
}
