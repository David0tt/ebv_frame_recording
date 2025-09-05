#include <gtest/gtest.h>
#include "event_camera_manager.h"
#include <unordered_map>

TEST(EventBias, DefaultLimitsPresence) {
    auto & limits = EventCameraManager::getDefaultBiasLimits();
    // Expect a few canonical keys
    EXPECT_NE(limits.find("bias_diff_on"), limits.end());
    EXPECT_NE(limits.find("bias_diff_off"), limits.end());
    EXPECT_NE(limits.find("bias_fo"), limits.end());
    EXPECT_NE(limits.find("bias_hpf"), limits.end());
    EXPECT_NE(limits.find("bias_refr"), limits.end());
}

TEST(EventBias, ValidateInsideRange) {
    auto & limits = EventCameraManager::getDefaultBiasLimits();
    for (const auto & kv : limits) {
        int mid = (kv.second.min_value + kv.second.max_value)/2;
        EXPECT_TRUE(EventCameraManager::testValidateBiasLimits(kv.first, mid));
    }
}

TEST(EventBias, ValidateOutsideRange) {
    auto & limits = EventCameraManager::getDefaultBiasLimits();
    for (const auto & kv : limits) {
        EXPECT_FALSE(EventCameraManager::testValidateBiasLimits(kv.first, kv.second.max_value + 100));
        EXPECT_FALSE(EventCameraManager::testValidateBiasLimits(kv.first, kv.second.min_value - 100));
    }
}

TEST(EventBias, ClipValues) {
    std::unordered_map<std::string,int> testVals {
        {"bias_diff_on", 9999}, // high
        {"bias_diff_off", -9999}, // low
        {"bias_fo", 10} // within range likely
    };
    auto clipped = EventCameraManager::testClipBiasValues(testVals);
    auto & limits = EventCameraManager::getDefaultBiasLimits();
    EXPECT_LE(clipped["bias_diff_on"], limits.at("bias_diff_on").max_value);
    EXPECT_GE(clipped["bias_diff_off"], limits.at("bias_diff_off").min_value);
    EXPECT_EQ(clipped["bias_fo"], 10);
}
