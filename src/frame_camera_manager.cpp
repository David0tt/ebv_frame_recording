#include <filesystem>
#include "frame_camera_manager.h"
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <chrono>
#include <peak/peak.hpp>
#include <peak_ipl/peak_ipl.hpp>
#include <peak/converters/peak_buffer_converter_ipl.hpp>
#include <opencv2/highgui.hpp>

FrameCameraManager::FrameCameraManager() {
    peak::Library::Initialize();
    const auto peakVersion = peak::Library::Version();
    std::cout << "Using PEAK SDK version: " << peakVersion.Major() << "." 
              << peakVersion.Minor() << "." << peakVersion.Subminor() << std::endl;
}

FrameCameraManager::~FrameCameraManager() {
    closeDevices();
    peak::Library::Close();
}

void FrameCameraManager::openAndSetupDevices() {
    auto& deviceManager = peak::DeviceManager::Instance();
    deviceManager.Update();

    if (deviceManager.Devices().empty()) {
        throw std::runtime_error("No frame camera device found.");
    }

    for (const auto& deviceDescriptor : deviceManager.Devices()) {
        auto device = deviceDescriptor->OpenDevice(peak::core::DeviceAccessType::Control);
        m_devices.push_back(device);
        setupDevice(device);
        std::cout << "Set up frame camera with serial number: " << deviceDescriptor->SerialNumber() << std::endl;
    }
}

void FrameCameraManager::setupDevice(std::shared_ptr<peak::core::Device> device) {
    auto remoteNodemap = device->RemoteDevice()->NodeMaps().at(0);

    // Configure camera settings
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("AcquisitionMode")->SetCurrentEntry("Continuous");
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("UserSetSelector")->SetCurrentEntry("Default");
    remoteNodemap->FindNode<peak::core::nodes::CommandNode>("UserSetLoad")->Execute();
    remoteNodemap->FindNode<peak::core::nodes::CommandNode>("UserSetLoad")->WaitUntilDone();
    remoteNodemap->FindNode<peak::core::nodes::FloatNode>("ExposureTime")->SetValue(10000.0);
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("GainSelector")->SetCurrentEntry("AnalogAll");
    remoteNodemap->FindNode<peak::core::nodes::FloatNode>("Gain")->SetValue(3.0);
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("TriggerSelector")->SetCurrentEntry("ExposureStart");
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("TriggerMode")->SetCurrentEntry("On");
    remoteNodemap->FindNode<peak::core::nodes::EnumerationNode>("TriggerSource")->SetCurrentEntry("Line0");
    remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("DeviceLinkThroughputLimit")->SetValue(300000000);

    auto dataStream = device->DataStreams().at(0)->OpenDataStream();
    m_dataStreams.push_back(dataStream);

    const auto payloadSize = remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("PayloadSize")->Value();
    const auto bufferCountMax = dataStream->NumBuffersAnnouncedMinRequired();

    for (size_t i = 0; i < bufferCountMax; ++i) {
        auto buffer = dataStream->AllocAndAnnounceBuffer(static_cast<size_t>(payloadSize), nullptr);
        dataStream->QueueBuffer(buffer);
    }

    remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")->SetValue(1);
}


void FrameCameraManager::startRecording(const std::string& outputPath) {
    m_acquiring = true;
    m_frames.resize(m_devices.size());

    for (size_t i = 0; i < m_dataStreams.size(); ++i) {
        m_dataStreams[i]->StartAcquisition();
        m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")->Execute();
        m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")->WaitUntilDone();
    }

    // Start the disk writer thread first
    m_diskWriterThread = std::thread(&FrameCameraManager::diskWriterWorker, this, outputPath);

    // Start acquisition threads (now they only capture, don't write to disk)
    for (size_t i = 0; i < m_devices.size(); ++i) {
        m_acquisitionThreads.emplace_back(&FrameCameraManager::acquisitionWorker, this, i);
    }
}

void FrameCameraManager::stopRecording() {
    if (!m_acquiring) {
        return;
    }
    
    m_acquiring = false;
    
    // Wait for acquisition threads to finish
    for (auto& thread : m_acquisitionThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_acquisitionThreads.clear();

    // Notify disk writer thread and wait for it to finish
    m_queueCondition.notify_all();
    if (m_diskWriterThread.joinable()) {
        m_diskWriterThread.join();
    }

    // Stop acquisition for all devices
    for (size_t i = 0; i < m_dataStreams.size(); ++i) {
        try {
            m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStop")->Execute();
            m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStop")->WaitUntilDone();
            m_dataStreams[i]->StopAcquisition(peak::core::AcquisitionStopMode::Default);
            m_dataStreams[i]->Flush(peak::core::DataStreamFlushMode::DiscardAll);

            for (const auto& buffer : m_dataStreams[i]->AnnouncedBuffers()) {
                m_dataStreams[i]->RevokeBuffer(buffer);
            }
            m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")->SetValue(0);
        } catch (const std::exception& e) {
            std::cerr << "Error stopping acquisition for device " << i << ": " << e.what() << std::endl;
        }
    }
}

void FrameCameraManager::acquisitionWorker(int deviceId) {
    static std::vector<int> frameIndices(m_devices.size(), 0);

    // FPS tracking variables
    auto lastFpsReport = std::chrono::steady_clock::now();
    int framesSinceLastReport = 0;
    constexpr auto FPS_REPORT_INTERVAL = std::chrono::seconds(1);

    while (m_acquiring) {
        try {
            auto buffer = m_dataStreams[deviceId]->WaitForFinishedBuffer(1000);
            
            if (!m_acquiring) {
                m_dataStreams[deviceId]->QueueBuffer(buffer);
                break;
            }
            
            const auto image = peak::BufferTo<peak::ipl::Image>(buffer).ConvertTo(
                peak::ipl::PixelFormatName::BGRa8, peak::ipl::ConversionMode::Fast);

            cv::Mat cvImage(
                image.Height(),
                image.Width(),
                CV_8UC4,
                image.Data(),
                image.Width() * 4 // BGRa8 is 4 bytes per pixel
            );

            // Create frame data and add to queue (non-blocking)
            FrameData frameData;
            frameData.image = cvImage.clone(); // Important: clone to create independent copy
            frameData.deviceId = deviceId;
            frameData.frameIndex = frameIndices[deviceId]++;
            frameData.timestamp = std::chrono::steady_clock::now();

            // Add frame to queue (with queue size limiting)
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (m_frameQueue.size() < MAX_QUEUE_SIZE) {
                    m_frameQueue.push(std::move(frameData));
                    m_queueCondition.notify_one();
                } else {
                    // Queue is full, drop oldest frame to make space
                    std::cerr << "Warning: Frame queue full for device " << deviceId 
                             << ", dropping oldest frame" << std::endl;
                    m_frameQueue.pop();
                    m_frameQueue.push(std::move(frameData));
                    m_queueCondition.notify_one();
                }
            }
            
            // Update FPS tracking
            framesSinceLastReport++;
            auto currentTime = std::chrono::steady_clock::now();
            auto timeSinceLastReport = currentTime - lastFpsReport;
            
            if (timeSinceLastReport >= FPS_REPORT_INTERVAL) {
                double elapsed_seconds = std::chrono::duration<double>(timeSinceLastReport).count();
                double fps = framesSinceLastReport / elapsed_seconds;
                std::cout << "Frame Camera " << deviceId << " FPS: " << std::fixed << std::setprecision(2) 
                         << fps << " (frames: " << framesSinceLastReport << " in " 
                         << std::setprecision(1) << elapsed_seconds << "s)" << std::endl;
                
                // Reset counters
                lastFpsReport = currentTime;
                framesSinceLastReport = 0;
            }
                    
            m_dataStreams[deviceId]->QueueBuffer(buffer);
        } catch (const std::exception& e) {
            std::cerr << "Acquisition error on device " << deviceId << ": " << e.what() << std::endl;
        }
    }
}

void FrameCameraManager::diskWriterWorker(const std::string& outputPath) {
    // Create output directories for each camera
    std::vector<std::filesystem::path> cameraDirs(m_devices.size());
    for (size_t i = 0; i < m_devices.size(); ++i) {
        cameraDirs[i] = std::filesystem::path(outputPath) / ("frame_cam" + std::to_string(i));
        std::filesystem::create_directories(cameraDirs[i]);
    }

    std::cout << "Disk writer thread started" << std::endl;

    while (m_acquiring || !m_frameQueue.empty()) {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // Wait for frames or stop signal
        m_queueCondition.wait(lock, [this] { 
            return !m_frameQueue.empty() || !m_acquiring; 
        });

        // Process all available frames
        while (!m_frameQueue.empty()) {
            FrameData frameData = std::move(m_frameQueue.front());
            m_frameQueue.pop();
            lock.unlock(); // Release lock while doing I/O

            try {
                // Write frame to disk
                const std::string filename = (cameraDirs[frameData.deviceId] / 
                    ("frame_" + std::to_string(frameData.frameIndex) + ".jpg")).string();
                cv::imwrite(filename, frameData.image);
            } catch (const std::exception& e) {
                std::cerr << "Error writing frame for device " << frameData.deviceId 
                         << ", frame " << frameData.frameIndex << ": " << e.what() << std::endl;
            }

            lock.lock(); // Reacquire lock for next iteration
        }
    }

    std::cout << "Disk writer thread finished" << std::endl;
}

void FrameCameraManager::closeDevices() {
    try {
        // First ensure recording is stopped
        stopRecording();
        
        std::cout << "Closing frame camera devices..." << std::endl;
        
        // Close data streams first
        for (auto& dataStream : m_dataStreams) {
            if (dataStream) {
                try {
                    dataStream->Flush(peak::core::DataStreamFlushMode::DiscardAll);
                } catch (const std::exception& e) {
                    std::cerr << "Error flushing data stream: " << e.what() << std::endl;
                }
            }
        }
        m_dataStreams.clear();
        
        // Close devices
        for (size_t i = 0; i < m_devices.size(); ++i) {
            if (m_devices[i]) {
                try {
                    auto remoteNodemap = m_devices[i]->RemoteDevice()->NodeMaps()[0];
                    remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")->SetValue(0);
                    std::cout << "Closed frame camera device " << i << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Error closing frame camera device " << i << ": " << e.what() << std::endl;
                }
            }
        }
        
        // Clear device list
        m_devices.clear();
        
        std::cout << "All frame camera devices closed and resources released" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error closing frame camera devices: " << e.what() << std::endl;
    }
}

