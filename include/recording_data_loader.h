#pragma once

#include <QObject>
#include <QImage>
#include <opencv2/opencv.hpp>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>

#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

struct FrameCameraData {
    std::vector<std::string> image_files; // sorted
    // Lazy load cache for current frame
    cv::Mat loadFrame(size_t idx) const {
        if (idx >= image_files.size()) return {};
        return cv::imread(image_files[idx], cv::IMREAD_UNCHANGED);
    }
};

struct EventCameraData {
    // Pre-generated event accumulation frames as QImages
    std::vector<QImage> frames;
};

struct RecordingData {
    std::vector<FrameCameraData> frameCams;
    std::vector<EventCameraData> eventCams;
    size_t totalFrames{1};
    std::string loadedPath;
    bool isValid{false};
};

class RecordingDataLoader : public QObject {
    Q_OBJECT

public:
    explicit RecordingDataLoader(QObject *parent = nullptr);
    ~RecordingDataLoader();

    // Async loading
    void loadRecording(const std::string &dirPath);
    void abortLoading();
    
    // Data access
    const RecordingData& getData() const { return m_data; }
    bool isDataReady() const { return m_dataReady.load(); }
    bool isLoading() const { return m_loading.load(); }

    // Frame access helpers
    cv::Mat getFrameCameraFrame(int camera, size_t frameIndex) const;
    QImage getEventCameraFrame(int camera, size_t frameIndex) const;

signals:
    void loadingStarted(const QString &path);
    void loadingFinished(bool success, const QString &message);
    void loadingProgress(const QString &status);

private:
    void loadDataWorker(const std::string &dirPath);
    void loadFrameCameraData(const std::string &dirPath, int camera, FrameCameraData &data);
    void loadEventCameraData(const std::string &dirPath, int camera, EventCameraData &data);
    size_t calculateTotalFrames() const;

    RecordingData m_data;
    std::thread m_loaderThread;
    std::atomic<bool> m_abortLoading{false};
    std::atomic<bool> m_dataReady{false};
    std::atomic<bool> m_loading{false};
};

// Utility functions moved from player_window
QImage cvMatToQImage(const cv::Mat &mat);
long long extract_frame_index(const std::string &pathStr);
