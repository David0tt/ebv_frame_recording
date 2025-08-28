#include "frame_camera_manager.h"
#include <iostream>
#include <stdexcept>
#include <peak/peak.hpp>
#include <peak_ipl/peak_ipl.hpp>
#include <peak/converters/peak_buffer_converter_ipl.hpp>
#include <opencv2/highgui.hpp>

FrameCameraManager::FrameCameraManager() {
    peak::Library::Initialize();
    auto peakVersion = peak::Library::Version();
    std::cout << "Using PEAK SDK version: " << peakVersion.Major() << "." << peakVersion.Minor() << "." << peakVersion.Subminor() << std::endl;
}

FrameCameraManager::~FrameCameraManager() {
    stopAcquisition();
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
        std::cout << "Set up device with serial number: " << deviceDescriptor->SerialNumber() << std::endl;
    }
}

void FrameCameraManager::setupDevice(std::shared_ptr<peak::core::Device> device) {
    auto remoteNodemap = device->RemoteDevice()->NodeMaps().at(0);


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

    auto dataStream = device->DataStreams().at(0)->OpenDataStream();
    m_dataStreams.push_back(dataStream);

    auto payloadSize = remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("PayloadSize")->Value();
    auto bufferCountMax = dataStream->NumBuffersAnnouncedMinRequired();

    for (size_t i = 0; i < bufferCountMax; ++i) {
        auto buffer = dataStream->AllocAndAnnounceBuffer(static_cast<size_t>(payloadSize), nullptr);
        dataStream->QueueBuffer(buffer);
    }

    remoteNodemap->FindNode<peak::core::nodes::IntegerNode>("TLParamsLocked")->SetValue(1);
}


void FrameCameraManager::startAcquisition() {
    m_acquiring = true;
    m_frames.resize(m_devices.size());

    for (size_t i = 0; i < m_dataStreams.size(); ++i) {
        m_dataStreams[i]->StartAcquisition();
        m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")->Execute();
        m_devices[i]->RemoteDevice()->NodeMaps()[0]->FindNode<peak::core::nodes::CommandNode>("AcquisitionStart")->WaitUntilDone();
    }

    for (size_t i = 0; i < m_devices.size(); ++i) {
        m_acquisitionThreads.emplace_back(&FrameCameraManager::acquisitionWorker, this, i);
    }
}

void FrameCameraManager::stopAcquisition() {
    m_acquiring = false;
    for (auto& thread : m_acquisitionThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_acquisitionThreads.clear();

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
    static int frameIndex = 0;

    while (m_acquiring) {
        try {
            auto buffer = m_dataStreams[deviceId]->WaitForFinishedBuffer(5000);
            
            const auto image = peak::BufferTo<peak::ipl::Image>(buffer).ConvertTo(
                peak::ipl::PixelFormatName::BGRa8, peak::ipl::ConversionMode::Fast);

            // Showing the image using OpenCV
            cv::Mat cvImage(
                image.Height(),
                image.Width(),
                CV_8UC4,
                image.Data(),
                image.Width() * 4 // BGRa8 is 4 bytes per pixel
            );

            // std::string windowName = "Frame Camera " + std::to_string(deviceId);
            // cv::imshow(windowName, cvImage);
            // cv::waitKey(1);

            // TODO it has to be made sure, that this file exists

            std::string filename = "./recording/frame_cam" + std::to_string(deviceId) + "_frame_" + std::to_string(frameIndex++) + ".jpg";
            cv::imwrite(filename, cvImage);

                    
            m_dataStreams[deviceId]->QueueBuffer(buffer);
        } catch (const std::exception& e) {
            std::cerr << "Acquisition error on device " << deviceId << ": " << e.what() << std::endl;
        }
    }
}

void FrameCameraManager::saveFrames(const std::string& path) {
    // for (size_t i = 0; i < m_frames.size(); ++i) {
    //     std::string devicePath = path + "/frame_cam" + std::to_string(i);
    //     // Create directory, C++17 filesystem would be better
    //     system(("mkdir -p " + devicePath).c_str());
    //     for (size_t j = 0; j < m_frames[i].size(); ++j) {
    //         std::string framePath = devicePath + "/frame_" + std::to_string(j) + ".png";
    //         cv::imwrite(framePath, m_frames[i][j]);
    //     }
    // }
}
