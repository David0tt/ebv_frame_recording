#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <csignal>
#include <atomic>
#include "frame_camera_manager.h"
#include "event_camera_manager.h"
#include <filesystem>
#include "CLI11.hpp"

// Global flag to signal shutdown
std::atomic<bool> shutdown_flag(false);

void signal_handler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    shutdown_flag = true;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler); // Register signal handler for Ctrl+C

    CLI::App app{"EBV and Frame Camera Recording System"};
    app.footer(
        "- IMPORTANT: For proper synchronization, ensure all cameras are connected via their GPIO ports and triggered by an external signal generator as specified in the project documentation. Without this setup, frame cameras will not record frames, and event cameras will not include trigger events in their data stream.\n"
        "- The lengths of all the bias settings and serial numbers have to be equal, the i-th bias setting corresponds to the i-th serial number."
    );

    std::vector<std::string> serials;
    app.add_option("-s,--serials", serials, "Serial numbers of event cameras (master first)");

    std::string output_prefix = "";
    app.add_option("-p,--prefix", output_prefix, "Prefix for the recording directory name");

    int recording_length = -1;
    app.add_option("-l,--length", recording_length, "Length of the recording in seconds. -1 for indefinite recording.");

    std::unordered_map<std::string, std::vector<int>> biases;
    app.add_option("--bias_diff_on", biases["bias_diff_on"], "bias_diff_on values");
    app.add_option("--bias_diff_off", biases["bias_diff_off"], "bias_diff_off values");
    app.add_option("--bias_fo", biases["bias_fo"], "bias_fo values");
    app.add_option("--bias_hpf", biases["bias_hpf"], "bias_hpf values");
    app.add_option("--bias_refr", biases["bias_refr"], "bias_refr values");

    CLI11_PARSE(app, argc, argv);

    std::cout << "Starting EBV and Frame Camera Recording System" << std::endl;

    // // Camera configuration
    // constexpr auto EBV_SERIAL_NUMBER0 = "4108900147";  // Master
    // constexpr auto EBV_SERIAL_NUMBER1 = "4108900356";  // Slave
    
    // Generate timestamp-based recording directory
    const auto now = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    const auto tm = *std::localtime(&time_t);
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    std::string outputDir = "./recording/";
    if (!output_prefix.empty()) {
        outputDir += output_prefix + "_";
    }
    outputDir += std::string(timestamp);
    std::filesystem::create_directories(outputDir);

    try {
        // Initialize managers
        FrameCameraManager frameCameraManager;
        EventCameraManager eventCameraManager;

        std::cout << "Setting up frame cameras..." << std::endl;
        frameCameraManager.openAndSetupDevices();

        std::cout << "Setting up event cameras..." << std::endl;
        
        // // Configure event cameras with default biases
        // const std::unordered_map<std::string, int> defaultBiases = {
        //     {"bias_diff_on", 0}, {"bias_diff_off", 0}, {"bias_fo", 0}, 
        //     {"bias_hpf", 0}, {"bias_refr", 0}
        // };
        
        // const std::vector<EventCameraManager::CameraConfig> cameraConfigs = {
        //     {EBV_SERIAL_NUMBER0, defaultBiases},
        //     {EBV_SERIAL_NUMBER1, defaultBiases}
        // };

        // Configure event cameras
        std::vector<EventCameraManager::CameraConfig> cameraConfigs;
        if (!serials.empty()) {
            // Validate that if any bias is provided, its vector has the same size as the serials vector
            for (auto const& [key, val] : biases) {
                if (!val.empty() && val.size() != serials.size()) {
                    throw std::runtime_error("Number of bias values for " + key + " must match number of serials.");
                }
            }

            for (size_t i = 0; i < serials.size(); ++i) {
                EventCameraManager::BiasConfig camera_biases;
                const std::unordered_map<std::string, int> defaultBiases = {
                    {"bias_diff_on", 0}, {"bias_diff_off", 0}, {"bias_fo", 0}, 
                    {"bias_hpf", 0}, {"bias_refr", 0}
                };

                for (auto const& [key, default_val] : defaultBiases) {
                    if (biases.count(key) && i < biases.at(key).size()) {
                        camera_biases[key] = biases.at(key)[i];
                    } else {
                        camera_biases[key] = default_val;
                    }
                }
                cameraConfigs.push_back({serials[i], camera_biases});
            }
        }

        
        eventCameraManager.openAndSetupDevices(cameraConfigs);

        std::cout << "Starting recording to: " << outputDir << std::endl;
        eventCameraManager.startRecording(outputDir);
        frameCameraManager.startRecording(outputDir);

        if (recording_length > 0) {
            std::cout << "Recording for " << recording_length << " seconds..." << std::endl;
            auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(recording_length);
            while (std::chrono::steady_clock::now() < end_time && !shutdown_flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            std::cout << "Recording indefinitely. Press Ctrl+C to stop." << std::endl;
            while (!shutdown_flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        std::cout << "Stopping recording..." << std::endl;
        frameCameraManager.stopRecording();
        eventCameraManager.stopRecording();

        std::cout << "Recording completed successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
