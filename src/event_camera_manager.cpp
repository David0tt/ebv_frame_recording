#include "event_camera_manager.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <metavision/sdk/stream/camera_exception.h>
#include <metavision/hal/device/device_discovery.h>

const std::unordered_map<std::string, BiasLimits> EventCameraManager::DEFAULT_BIAS_LIMITS = {
    {"bias_diff_on", {-85, 140}},
    {"bias_diff_off", {-35, 190}},
    {"bias_fo", {-35, 55}},
    {"bias_hpf", {0, 120}},
    {"bias_refr", {-20, 235}}
};

const std::unordered_map<std::string, int> EventCameraManager::DEFAULT_BIASES = {
    {"bias_diff_on", 0},
    {"bias_diff_off", 0},
    {"bias_fo", 0},
    {"bias_hpf", 0},
    {"bias_refr", 0}
};


EventCameraManager::EventCameraManager() : m_recording(false) {
    // Reserve space for typical setup, but can grow as needed
    m_cameras.reserve(2);
}

EventCameraManager::~EventCameraManager() {
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
            
            if (!m_cameras[i]->start()) {
                throw std::runtime_error("Failed to start camera " + std::to_string(i));
            }
            
            const std::string capitalizedType = (i == 0) ? "Master" : "Slave";
            std::cout << capitalizedType << " camera " << i << " started" << std::endl;
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
        for (size_t i = 0; i < m_cameras.size(); ++i) {
            if (m_cameras[i]) {
                m_cameras[i]->stop_recording();
                m_cameras[i]->stop();
                const std::string cameraType = (i == 0) ? "master" : "slave";
                std::cout << "Stopped " << cameraType << " camera " << i << std::endl;
            }
        }
        m_recording = false;
        std::cout << "Event camera recording stopped successfully for " << m_cameras.size() << " cameras" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error stopping cameras: " << e.what() << std::endl;
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
