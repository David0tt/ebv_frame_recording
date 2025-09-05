#include <gtest/gtest.h>
#include "recording_loader.h" // for extract_frame_index declaration

// Forward declare if not in header (utility lives in cpp)
long long extract_frame_index(const std::string &pathStr);

struct ExtractFrameIndexCase { std::string name; long long expected; };

TEST(ExtractFrameIndex, BasicPatterns) {
    std::vector<ExtractFrameIndexCase> cases = {
        {"frame_00001.jpg", 1},
        {"/tmp/data/frame_123.png", 123},
        {"relative/path/img42.jpeg", 42},
        {"multi_99_end7.png", 7},
        {"nondigits.txt", -1},
        {"frame_9999999999999999999999999.jpg", -1} // overflow -> expect -1 catch
    };
    for (auto &c : cases) {
        EXPECT_EQ(extract_frame_index(c.name), c.expected) << c.name;
    }
}

TEST(ExtractFrameIndex, TrailingDotAndNoExtension) {
    EXPECT_EQ(extract_frame_index("frame_12."), 12);
    EXPECT_EQ(extract_frame_index("frame_77"), 77);
}
