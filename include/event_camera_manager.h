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
#include <metavision/hal/device/device_discovery.h>

struct BiasLimits {
    int min_value;
    int max_value;
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
};
