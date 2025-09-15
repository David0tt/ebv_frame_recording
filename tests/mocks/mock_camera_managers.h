// Google Mock based mocks for RecordingManager dependencies (Phase 3)
#pragma once

#include <gmock/gmock.h>
#include "recording_manager.h"

class MockFrameCameraManager : public RecordingManager::IFrameCameraManager {
public:
    MOCK_METHOD(void, openAndSetupDevices, (), (override));
    MOCK_METHOD(void, startRecording, (const std::string& outputPath), (override));
    MOCK_METHOD(void, stopRecording, (), (override));
    MOCK_METHOD(void, closeDevices, (), (override));
    MOCK_METHOD(bool, getLatestFrame, (int deviceId, FrameData& frameData), (override));
    MOCK_METHOD(void, startPreview, (), (override));
    MOCK_METHOD(void, stopPreview, (), (override));
    MOCK_METHOD(void, startRecordingToPath, (const std::string&), (override));
    MOCK_METHOD(void, stopRecordingOnly, (), (override));
};

class MockEventCameraManager : public RecordingManager::IEventCameraManager {
public:
    MOCK_METHOD(void, openAndSetupDevices, (const std::vector<CameraConfig>& cameraConfigs), (override));
    MOCK_METHOD(void, startRecording, (const std::string& outputPath, const std::string& fileFormat), (override));
    MOCK_METHOD(void, stopRecording, (), (override));
    MOCK_METHOD(void, closeDevices, (), (override));
    MOCK_METHOD(bool, startLiveStreaming, (), (override));
    MOCK_METHOD(void, stopLiveStreaming, (), (override));
    MOCK_METHOD(bool, getLatestEventFrame, (int cameraId, cv::Mat& eventFrame, size_t& frameIndex), (override));
};
