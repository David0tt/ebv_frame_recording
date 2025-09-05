#include <gtest/gtest.h>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include <QCoreApplication>
#include "recording_loader.h"

namespace fs = std::filesystem;

static fs::path createOutOfOrderDir() {
    auto tmp = fs::temp_directory_path()/fs::path("ebv_sort_test_"+std::to_string(::getpid())+"_"+std::to_string(rand()));
    fs::create_directories(tmp/"frame_cam0");
    // Intentionally shuffled indices & names
    std::vector<std::string> names = {"frame_10.jpg", "frame_2.jpg", "frame_1.jpg", "frame_02.jpg"};
    int shade=0;
    for(auto &n : names){
        cv::Mat img(4,4,CV_8UC3, cv::Scalar(shade,shade,shade)); shade+=10;
        cv::imwrite((tmp/"frame_cam0"/n).string(), img);
    }
    return tmp;
}

TEST(RecordingLoaderSorting, OrdersByNumericComponentThenLexicographic) {
    int argc=0; char** argv=nullptr; if(!QCoreApplication::instance()) new QCoreApplication(argc, argv);
    auto dir = createOutOfOrderDir();
    RecordingLoader loader; loader.loadRecording(dir.string());
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!loader.isDataReady() && std::chrono::steady_clock::now()<deadline){ QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    ASSERT_TRUE(loader.isDataReady());
    auto files = loader.getData().frameCams[0].image_files;
    // Expected order: frame_1, frame_2, frame_02 (tie idx=2 then lexicographic), frame_10
    ASSERT_EQ(files.size(), 4u);
    EXPECT_TRUE(files[0].find("frame_1.jpg")!=std::string::npos);
    EXPECT_TRUE(files[1].find("frame_2.jpg")!=std::string::npos);
    EXPECT_TRUE(files[2].find("frame_02.jpg")!=std::string::npos);
    EXPECT_TRUE(files[3].find("frame_10.jpg")!=std::string::npos);
}
