#pragma once

#include <QObject>
#include <QImage>
#include <QSet>
#include <opencv2/opencv.hpp>
#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>
#include <metavision/sdk/base/events/event_cd.h>

#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <atomic>

struct FrameCameraData {
    std::vector<std::string> image_files; // sorted
    // Lazy load cache for current frame
    cv::Mat loadFrame(size_t idx) const {
        if (idx >= image_files.size()) return {};
        return cv::imread(image_files[idx], cv::IMREAD_UNCHANGED);
    }
};

// Forward declaration
class EventCameraLoader;

struct EventCameraData {
    std::string filePath;
    std::unique_ptr<EventCameraLoader> loader;
    size_t estimatedFrameCount{0};
    int width{0};
    int height{0};
    bool isValid{false};
};

// Efficient event camera frame loader with lazy generation
class EventCameraLoader {
public:
    EventCameraLoader(const std::string &filePath);
    ~EventCameraLoader();
    
    // Get frame at specific time index (lazy generation)
    QImage getFrame(size_t frameIndex, double fps = 30.0);
    // Notify loader of externally updated playback position (optional helper)
    void setCurrentFrameIndex(size_t frameIndex);
    void setPlaybackFps(double fps);
    
    // Get metadata
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    size_t getEstimatedFrameCount() const { return m_estimatedFrameCount; }
    bool isValid() const { return m_isValid; }
    
    // Get cached frame indices
    QSet<int> getCachedFrames() const;
    
private:
    void initialize();
    QImage generateFrameFromTimeRange(Metavision::timestamp startTime, Metavision::timestamp endTime);
    QImage generateFrameFromEvents(const std::vector<Metavision::EventCD> &events);
    void prefetchThreadMain();
    void requestPrefetch();
    
    std::string m_filePath;
    
    int m_width{0};
    int m_height{0};
    size_t m_estimatedFrameCount{0};
    bool m_isValid{false};
    
    // Frame cache only (no event pre-loading)
    std::unordered_map<size_t, QImage> m_frameCache;
    mutable std::mutex m_frameMutex;
    static const size_t MAX_CACHE_SIZE = 10000;
    static const size_t PREFETCH_AHEAD_FRAMES = MAX_CACHE_SIZE / 2; // how many future frames to pre-generate

    // Prefetch machinery
    std::thread m_prefetchThread;
    std::atomic<bool> m_stopPrefetch{false};
    std::atomic<size_t> m_currentFrameIndex{0};
    std::atomic<double> m_fps{30.0};
    std::mutex m_prefetchMutex;
    std::condition_variable m_prefetchCv;
    bool m_prefetchDirty{false};
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
    
    // Cache information helpers
    QSet<int> getCachedEventFrames(int camera) const;
    QSet<int> getAllCachedFrames() const;
    
    // Prefetch control
    void notifyFrameChanged(size_t frameIndex);

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
