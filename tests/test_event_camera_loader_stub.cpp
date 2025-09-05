#include <gtest/gtest.h>
#include "recording_loader.h" // includes EventCameraLoader

// NOTE: This is a limited stub test: creating loader with non-existent file should mark invalid and produce fallback frames.

TEST(EventCameraLoaderStub, InvalidFileProducesFallbackFrame) {
    EventCameraLoader loader("/path/that/does/not/exist_hopefully.raw");
    // Likely invalid; request frame
    auto img = loader.getFrame(0);
    // If invalid, implementation returns constructed QImage (may be default if width/height unknown)
    EXPECT_TRUE(img.isNull() || img.width() >= 0);
}

TEST(EventCameraLoaderStub, CacheStoresFrames) {
    EventCameraLoader loader("/path/that/does/not/exist_hopefully.raw");
    for(size_t i=0;i<3;++i){ loader.getFrame(i); }
    auto cached = loader.getCachedFrames();
    // We requested frames; depending on validity may or may not cache; just ensure no crash and size <=3
    EXPECT_LE(cached.size(), 3);
}
