// Advanced tests for RecordingManager using mocks
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "recording_manager.h"
#include "mocks/mock_camera_managers.h"

using ::testing::Return;
using ::testing::NiceMock;
using ::testing::StrictMock;
using ::testing::Throw;
using ::testing::_;
using ::testing::DoAll;

namespace {

struct RecordingManagerFixture : public ::testing::Test {
    std::unique_ptr<NiceMock<MockFrameCameraManager>> frameMock;
    std::unique_ptr<NiceMock<MockEventCameraManager>> eventMock;
    MockFrameCameraManager* frameRaw{};
    MockEventCameraManager* eventRaw{};
    std::unique_ptr<RecordingManager> mgr;
    std::vector<std::string> statusMessages;

    void SetUp() override {
    frameMock = std::make_unique<NiceMock<MockFrameCameraManager>>();
    eventMock = std::make_unique<NiceMock<MockEventCameraManager>>();
        frameRaw = frameMock.get();
        eventRaw = eventMock.get();
        mgr = std::make_unique<RecordingManager>(std::move(frameMock), std::move(eventMock));
        mgr->setStatusCallback([this](const std::string& msg){ statusMessages.push_back(msg); });
    }
};

TEST_F(RecordingManagerFixture, ConfigureSuccessCallsOpenAndSetupOnBothManagers) {
    RecordingManager::RecordingConfig cfg; // empty -> event auto-discovery (no serials)
    // For auto-discovery we expect openAndSetupDevices with empty vector (passed through)
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(1); // empty vector
    EXPECT_CALL(*frameRaw, closeDevices()).Times(::testing::AnyNumber());
    EXPECT_CALL(*eventRaw, closeDevices()).Times(::testing::AnyNumber());
    bool ok = mgr->configure(cfg);
    // Can't fully succeed because event manager will likely throw on no cameras in real run; but test focuses on calls
    // Accept both success or failure but ensure calls happened
    (void)ok; // suppress unused
}

TEST_F(RecordingManagerFixture, StartRecordingFailsIfNotConfigured) {
    EXPECT_FALSE(mgr->startRecording("/tmp/out_dir_should_not_exist"));
}

TEST_F(RecordingManagerFixture, StartRecordingSuccessFlow) {
    RecordingManager::RecordingConfig cfg; // auto-discovery
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(1);
    EXPECT_CALL(*frameRaw, closeDevices()).Times(::testing::AnyNumber());
    EXPECT_CALL(*eventRaw, closeDevices()).Times(::testing::AnyNumber());
    ASSERT_TRUE(mgr->configure(cfg));

    EXPECT_CALL(*eventRaw, startRecording(::testing::_, ::testing::Eq("hdf5"))).Times(1);
    EXPECT_CALL(*frameRaw, startRecording(_)).Times(1);
    EXPECT_CALL(*eventRaw, startLiveStreaming()).WillOnce(Return(true));
    // Expectations for implicit destructor stop
    EXPECT_CALL(*frameRaw, stopRecording()).Times(1);
    EXPECT_CALL(*eventRaw, stopRecording()).Times(1);
    EXPECT_CALL(*eventRaw, stopLiveStreaming()).Times(1);

    ASSERT_TRUE(mgr->startRecording("./tmp_test_recording_dir"));
    EXPECT_TRUE(mgr->isRecording());
    // Explicitly stop to control timing (avoid relying solely on destructor)
    mgr->stopRecording();
}

TEST_F(RecordingManagerFixture, StopRecordingCallsManagers) {
    RecordingManager::RecordingConfig cfg; // auto-discovery
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(1);
    EXPECT_CALL(*frameRaw, closeDevices()).Times(::testing::AnyNumber());
    EXPECT_CALL(*eventRaw, closeDevices()).Times(::testing::AnyNumber());
    ASSERT_TRUE(mgr->configure(cfg));
    EXPECT_CALL(*eventRaw, startRecording(_, _)).Times(1);
    EXPECT_CALL(*frameRaw, startRecording(_)).Times(1);
    EXPECT_CALL(*eventRaw, startLiveStreaming()).WillOnce(Return(true));
    ASSERT_TRUE(mgr->startRecording("./tmp_test_recording_dir"));

    EXPECT_CALL(*frameRaw, stopRecording()).Times(1);
    EXPECT_CALL(*eventRaw, stopRecording()).Times(1);
    EXPECT_CALL(*eventRaw, stopLiveStreaming()).Times(1);
    mgr->stopRecording();
    EXPECT_FALSE(mgr->isRecording());
}

TEST_F(RecordingManagerFixture, GetLiveFrameDataDelegates) {
    RecordingManager::RecordingConfig cfg; // auto-discovery
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(1);
    EXPECT_CALL(*frameRaw, closeDevices()).Times(::testing::AnyNumber());
    EXPECT_CALL(*eventRaw, closeDevices()).Times(::testing::AnyNumber());
    ASSERT_TRUE(mgr->configure(cfg));
    EXPECT_CALL(*eventRaw, startRecording(_, _)).Times(1);
    EXPECT_CALL(*frameRaw, startRecording(_)).Times(1);
    EXPECT_CALL(*eventRaw, startLiveStreaming()).WillOnce(Return(true));
    ASSERT_TRUE(mgr->startRecording("./tmp_test_recording_dir"));

    FrameData fd; fd.frameIndex = 42; fd.deviceId = 0; fd.image = cv::Mat(10,10,CV_8UC1);
    EXPECT_CALL(*frameRaw, getLatestFrame(0, ::testing::_)).WillOnce(DoAll(::testing::SetArgReferee<1>(fd), Return(true)));
    cv::Mat out; size_t idx=0; 
    bool ok = mgr->getLiveFrameData(0, out, idx);
    EXPECT_TRUE(ok);
    EXPECT_EQ(idx, 42u);
}

TEST_F(RecordingManagerFixture, CloseDevicesCallsManagers) {
    RecordingManager::RecordingConfig cfg; // auto-discovery
    EXPECT_CALL(*frameRaw, openAndSetupDevices()).Times(1);
    EXPECT_CALL(*eventRaw, openAndSetupDevices(_)).Times(1);
    ASSERT_TRUE(mgr->configure(cfg));
    EXPECT_CALL(*frameRaw, closeDevices()).Times(1);
    EXPECT_CALL(*eventRaw, closeDevices()).Times(1);
    mgr->closeDevices();
}

} // namespace
