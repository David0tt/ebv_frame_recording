#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <metavision/sdk/stream/camera.h>
#include <metavision/hal/facilities/i_camera_synchronization.h>
#include <metavision/hal/facilities/i_ll_biases.h>

struct BiasLimits {
    int min_value;
    int max_value;
};

class EventCameraManager {
public:
    EventCameraManager();
    ~EventCameraManager();

    // Open and setup event cameras
    void openDevices(const std::string& serialMaster, const std::string& serialSlave);
    
    // Set biases for both cameras
    void setBiases(const std::unordered_map<std::string, int>& biasesMaster, 
                   const std::unordered_map<std::string, int>& biasesSlave);
    
    // Start recording to files
    void startRecording(const std::string& outputPath);
    
    // Stop recording
    void stopRecording();

private:
    void clipBiasValues(std::unordered_map<std::string, int>& biases);
    bool validateBiasLimits(const std::string& biasName, int value);
    
    // Default bias limits
    static const std::unordered_map<std::string, BiasLimits> DEFAULT_BIAS_LIMITS;
    
    std::vector<std::unique_ptr<Metavision::Camera>> m_cameras; // index 0 master, 1 slave
    
    std::atomic<bool> m_recording;
    std::string m_outputPath;
};
