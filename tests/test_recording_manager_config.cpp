#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "recording_manager.h"
#include "mocks/mock_camera_managers.h"

using ::testing::Return;
using ::testing::NiceMock;
using ::testing::_;
using ::testing::DoAll;

struct RecordingManagerConfigFixture : public ::testing::Test {
    std::unique_ptr<NiceMock<MockFrameCameraManager>> frameMock;
    std::unique_ptr<NiceMock<MockEventCameraManager>> eventMock;
    MockFrameCameraManager* frameRaw{}; MockEventCameraManager* eventRaw{};
    std::unique_ptr<RecordingManager> mgr;
    void SetUp() override {
        frameMock = std::make_unique<NiceMock<MockFrameCameraManager>>();
        eventMock = std::make_unique<NiceMock<MockEventCameraManager>>();
        frameRaw = frameMock.get(); eventRaw = eventMock.get();
        mgr = std::make_unique<RecordingManager>(std::move(frameMock), std::move(eventMock));
    }
};

TEST_F(RecordingManagerConfigFixture, InvalidEventFileFormatFailsStart) {
    RecordingManager::RecordingConfig cfg; cfg.eventFileFormat = "badfmt"; // invalid
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(0);
    EXPECT_FALSE(mgr->startRecording("./tmp_dir", cfg));
}

TEST_F(RecordingManagerConfigFixture, BiasVectorMismatchThrowsConfigure) {
    RecordingManager::RecordingConfig cfg; cfg.eventCameraSerials = {"ABC","DEF"};
    cfg.biases["bias_diff_on"] = {1}; // mismatch length (1 vs 2)
    // configure() validates file format first, then proceeds to open frame devices
    // before building event camera configs where the mismatch triggers an exception.
    // So frame manager will be opened exactly once, event manager not at all.
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(0);
    EXPECT_FALSE(mgr->configure(cfg)); // should return false after catching exception
}

TEST_F(RecordingManagerConfigFixture, BiasMappingAppliesDefaultsAndOverrides) {
    RecordingManager::RecordingConfig cfg; cfg.eventCameraSerials = {"S1","S2"};
    cfg.biases["bias_diff_on"] = {5,6};
    // Capture argument passed into event openAndSetupDevices
    std::vector<RecordingManager::IEventCameraManager::CameraConfig> captured;
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).WillOnce(::testing::Invoke([&](const auto& v){ captured = v; }));
    EXPECT_TRUE(mgr->configure(cfg));
    ASSERT_EQ(captured.size(), 2u);
    EXPECT_EQ(captured[0].biases.at("bias_diff_on"), 5);
    EXPECT_EQ(captured[1].biases.at("bias_diff_on"), 6);
    // A default bias (e.g. bias_hpf) should exist with default value 0
    EXPECT_EQ(captured[0].biases.at("bias_hpf"), 0);
}
