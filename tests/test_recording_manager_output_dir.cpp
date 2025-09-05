#include <gtest/gtest.h>
#include "recording_manager.h"
#include <regex>

TEST(RecordingManagerOutputDir, PatternWithAndWithoutPrefix) {
    RecordingManager mgr; // uses adapters but we only call helper
    auto with = mgr.testGenerateOutputDirectory("pre");
    auto without = mgr.testGenerateOutputDirectory("");
    std::regex reWith(R"(^\./recording/pre_\d{8}_\d{6}$)");
    std::regex reWithout(R"(^\./recording/\d{8}_\d{6}$)");
    EXPECT_TRUE(std::regex_match(with, reWith)) << with;
    EXPECT_TRUE(std::regex_match(without, reWithout)) << without;
}
