#include "event_camera_manager.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <metavision/sdk/stream/camera_exception.h>

// Default bias limits from the Python script
const std::unordered_map<std::string, BiasLimits> EventCameraManager::DEFAULT_BIAS_LIMITS = {
    {"bias_diff_on", {-85, 140}},
    {"bias_diff_off", {-35, 190}},
    {"bias_fo", {-35, 55}},
    {"bias_hpf", {0, 120}},
    {"bias_refr", {-20, 235}}
};

EventCameraManager::EventCameraManager() : m_recording(false) {
    m_cameras.reserve(2);
}

EventCameraManager::~EventCameraManager() {
    stopRecording();
}

void EventCameraManager::openDevices(const std::string& serialMaster, const std::string& serialSlave) {
    try {
        // Clear any existing cameras
        if (!m_cameras.empty()) {
            m_cameras.clear();
        }

        // Open master camera
        auto camMaster = std::make_unique<Metavision::Camera>(Metavision::Camera::from_serial(serialMaster));
        auto camSlave  = std::make_unique<Metavision::Camera>(Metavision::Camera::from_serial(serialSlave));

        // Configure synchronization - Master camera
        auto& syncMaster = camMaster->get_facility<Metavision::I_CameraSynchronization>();
        if (!syncMaster.set_mode_master()) {
            throw std::runtime_error("Failed to set master camera synchronization mode");
        }
        std::cout << "Master camera set to master mode" << std::endl;

        // Configure synchronization - Slave camera  
        auto& syncSlave = camSlave->get_facility<Metavision::I_CameraSynchronization>();
        if (!syncSlave.set_mode_slave()) {
            throw std::runtime_error("Failed to set slave camera synchronization mode");
        }
        std::cout << "Slave camera set to slave mode" << std::endl;

        // Enable synchronization modes
        std::cout << "Master sync mode: " << static_cast<int>(syncMaster.get_mode()) << std::endl;
        std::cout << "Slave sync mode: " << static_cast<int>(syncSlave.get_mode()) << std::endl;

        // Store cameras (master first, then slave)
        m_cameras.push_back(std::move(camMaster)); // index 0 master
        m_cameras.push_back(std::move(camSlave));  // index 1 slave

        std::cout << "Successfully opened and configured " << m_cameras.size() << " event cameras" << std::endl;

    } catch (const Metavision::CameraException& e) {
        throw std::runtime_error("Failed to open event cameras: " + std::string(e.what()));
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to configure event cameras: " + std::string(e.what()));
    }
}

void EventCameraManager::setBiases(const std::unordered_map<std::string, int>& biasesMaster, 
                                   const std::unordered_map<std::string, int>& biasesSlave) {
    if (m_cameras.size() != 2) {
        throw std::runtime_error("Cameras must be opened before setting biases");
    }

    auto& biases0 = m_cameras[0]->get_facility<Metavision::I_LL_Biases>();
    auto& biases1 = m_cameras[1]->get_facility<Metavision::I_LL_Biases>();

    // Set biases for master camera
    for (const auto& [bias_name, bias_value] : biasesMaster) {
        if (validateBiasLimits(bias_name, bias_value)) {
            if (!biases0.set(bias_name, bias_value)) {
                std::cerr << "Warning: Failed to set bias " << bias_name << " to " << bias_value 
                         << " for master camera" << std::endl;
            } else {
                std::cout << "Master camera: Set " << bias_name << " = " << bias_value << std::endl;
            }
        }
    }

    // Set biases for slave camera
    for (const auto& [bias_name, bias_value] : biasesSlave) {
        if (validateBiasLimits(bias_name, bias_value)) {
            if (!biases1.set(bias_name, bias_value)) {
                std::cerr << "Warning: Failed to set bias " << bias_name << " to " << bias_value 
                         << " for slave camera" << std::endl;
            } else {
                std::cout << "Slave camera: Set " << bias_name << " = " << bias_value << std::endl;
            }
        }
    }
}

void EventCameraManager::startRecording(const std::string& outputPath) {
    if (m_cameras.size() != 2) {
        throw std::runtime_error("Cameras must be opened before starting recording");
    }

    m_outputPath = outputPath;
    
    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputPath);

    try {
        // Set up recording files
        std::string filename0 = outputPath + "/ebv_cam_0.raw";
        std::string filename1 = outputPath + "/ebv_cam_1.raw";

        // Start recording for both cameras
        if (!m_cameras[0]->start_recording(filename0)) {
            throw std::runtime_error("Failed to start recording for master camera");
        }
        std::cout << "Started recording master camera to: " << filename0 << std::endl;

        if (!m_cameras[1]->start_recording(filename1)) {
            throw std::runtime_error("Failed to start recording for slave camera");
        }
        std::cout << "Started recording slave camera to: " << filename1 << std::endl;

        // Start the cameras
        if (!m_cameras[0]->start()) {
            throw std::runtime_error("Failed to start master camera");
        }
        std::cout << "Master camera started" << std::endl;

        if (!m_cameras[1]->start()) {
            throw std::runtime_error("Failed to start slave camera");
        }
        std::cout << "Slave camera started" << std::endl;

        m_recording = true;
        std::cout << "Event camera recording started successfully" << std::endl;

    } catch (const Metavision::CameraException& e) {
        throw std::runtime_error("Failed to start recording: " + std::string(e.what()));
    }
}

void EventCameraManager::stopRecording() {
    if (m_recording && !m_cameras.empty()) {
        try {
            // Stop recording for both cameras
            for (size_t i = 0; i < m_cameras.size(); ++i) {
                if (m_cameras[i]) {
                    m_cameras[i]->stop_recording();
                    m_cameras[i]->stop();
                    std::cout << "Stopped camera " << i << std::endl;
                }
            }
            m_recording = false;
            std::cout << "Event camera recording stopped successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error stopping cameras: " << e.what() << std::endl;
        }
    }
}

void EventCameraManager::clipBiasValues(std::unordered_map<std::string, int>& biases) {
    for (auto& [bias_name, bias_value] : biases) {
        auto it = DEFAULT_BIAS_LIMITS.find(bias_name);
        if (it != DEFAULT_BIAS_LIMITS.end()) {
            bias_value = std::max(it->second.min_value, std::min(bias_value, it->second.max_value));
        }
    }
}

bool EventCameraManager::validateBiasLimits(const std::string& biasName, int value) {
    auto it = DEFAULT_BIAS_LIMITS.find(biasName);
    if (it != DEFAULT_BIAS_LIMITS.end()) {
        if (value < it->second.min_value || value > it->second.max_value) {
            std::cerr << "Warning: Bias " << biasName << " value " << value 
                     << " is outside limits [" << it->second.min_value 
                     << ", " << it->second.max_value << "]" << std::endl;
            return false;
        }
    }
    return true;
}
