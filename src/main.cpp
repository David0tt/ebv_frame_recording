#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include "frame_camera_manager.h"
#include "event_camera_manager.h"
#include <filesystem>

int main() {
    std::cout << "Starting EBV and Frame Camera Recording System" << std::endl;

    // Create managers
    FrameCameraManager frameCameraManager;
    EventCameraManager eventCameraManager;

    // Configuration from unified_recording_script.py
    const std::string EBV_SERIAL_NUMBER0 = "4108900147";  // Master
    const std::string EBV_SERIAL_NUMBER1 = "4108900356";  // Slave
    
    // Default biases from the Python script
    std::unordered_map<std::string, int> DEFAULT_BIASES = {
        {"bias_diff_on", 0},
        {"bias_diff_off", 0},
        {"bias_fo", 0},
        {"bias_hpf", 0},
        {"bias_refr", 0}
    };

    // Generate timestamp-based recording ID
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    std::string outputDir = "./recording/" + std::string(timestamp);
    std::filesystem::create_directories(outputDir);

    try {
        std::cout << "Setting up frame cameras..." << std::endl;
        frameCameraManager.openAndSetupDevices();

        std::cout << "Setting up event cameras..." << std::endl;
        eventCameraManager.openDevices(EBV_SERIAL_NUMBER0, EBV_SERIAL_NUMBER1);
        
        std::cout << "Setting biases for event cameras..." << std::endl;
        eventCameraManager.setBiases(DEFAULT_BIASES, DEFAULT_BIASES);

        std::cout << "Starting recording to: " << outputDir << std::endl;
        eventCameraManager.startRecording(outputDir);
        frameCameraManager.startAcquisition();

        std::cout << "Recording for 10 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));

        std::cout << "Stopping recording..." << std::endl;
        frameCameraManager.stopAcquisition();
        eventCameraManager.stopRecording();

        std::cout << "Recording completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}