#pragma once

#include <QObject>
#include <QImage>
#include <QSet>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <functional>
#include <chrono>

// Forward declarations
class RecordingLoader;
// Note: RecordingManager forward declaration without including header to avoid hardware dependencies in GUI

// Data structures for buffered frame data
struct BufferedFrameData {
    cv::Mat image;
    int cameraId;
    size_t frameIndex;
    std::chrono::steady_clock::time_point timestamp;
    bool isValid{false};
};

struct BufferedEventData {
    QImage frame;
    int cameraId;
    size_t frameIndex;
    std::chrono::steady_clock::time_point timestamp;
    bool isValid{false};
};

// Unified data structure that can represent both live and recorded data
struct UnifiedFrameData {
    std::vector<BufferedFrameData> frameData;  // Frame camera data (typically 2 cameras)
    std::vector<BufferedEventData> eventData;  // Event camera data (typically 2 cameras)
    size_t frameIndex{0};
    std::chrono::steady_clock::time_point timestamp;
    bool isValid{false};
};

class RecordingBuffer : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Playback,  // Reading from recorded data via RecordingLoader
        Live       // Reading from live recording via RecordingManager
    };

    explicit RecordingBuffer(QObject *parent = nullptr);
    ~RecordingBuffer();

    // Mode switching
    void setPlaybackMode(RecordingLoader* dataLoader);
    void setLiveMode(void* recordingManager);  // Using void* to avoid including hardware headers
    void stop();
    
    Mode getCurrentMode() const { return m_currentMode; }
    bool isActive() const { return m_active; }

    // Data access interface (unified for both modes)
    cv::Mat getFrameCameraFrame(int camera, size_t frameIndex) const;
    QImage getEventCameraFrame(int camera, size_t frameIndex) const;
    UnifiedFrameData getCurrentFrameData() const;
    
    // Playback-specific methods
    size_t getTotalFrames() const;
    void setCurrentFrameIndex(size_t frameIndex);
    size_t getCurrentFrameIndex() const { return m_currentFrameIndex; }
    
    // Live recording specific methods
    size_t getLiveFrameCount() const;
    UnifiedFrameData getLatestLiveData() const;
    
    // Cache information (for timeline visualization)
    QSet<int> getCachedFrames() const;
    
    // Performance monitoring
    double getCurrentFPS() const { return m_currentFPS; }
    size_t getBufferSize() const;
    bool isBufferHealthy() const;

signals:
    void frameDataUpdated(size_t frameIndex);
    void liveDataAvailable(const UnifiedFrameData& data);
    void bufferStatusChanged(bool healthy);
    void modeChanged(Mode newMode);

private slots:
    void onRecordingDataReady();

private:
    // Live mode implementation
    void startLiveBuffering();
    void stopLiveBuffering();
    void liveBufferWorker();
    void processLiveFrameData();
    void processLiveEventData();
    
    // Playback mode implementation
    void setupPlaybackMode();
    
    // Common helpers
    void updateFPS();
    void cleanupOldFrames();
    UnifiedFrameData createUnifiedFrame(size_t frameIndex, const std::chrono::steady_clock::time_point& timestamp) const;
    
    // Current mode and state
    Mode m_currentMode{Mode::Playback};
    std::atomic<bool> m_active{false};
    
    // External data sources
    RecordingLoader* m_dataLoader{nullptr};
    void* m_recordingManager{nullptr};  // Using void* to avoid including hardware headers
    
    // Current frame tracking
    std::atomic<size_t> m_currentFrameIndex{0};
    mutable std::mutex m_currentDataMutex;
    UnifiedFrameData m_currentFrameData;
    
    // Live mode buffering
    std::thread m_liveBufferThread;
    std::atomic<bool> m_stopBuffering{false};
    
    // Live data buffers
    std::queue<BufferedFrameData> m_liveFrameBuffer;
    std::queue<BufferedEventData> m_liveEventBuffer;
    mutable std::mutex m_liveBufferMutex;
    std::condition_variable m_liveBufferCondition;
    
    // Buffer management
    static constexpr size_t MAX_LIVE_BUFFER_SIZE = 500;  // Maximum frames to keep in live buffer
    static constexpr size_t TARGET_BUFFER_SIZE = 100;    // Target buffer size for healthy operation
    
    // Performance tracking
    mutable std::mutex m_fpsMutex;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    size_t m_lastFrameCount{0};
    std::atomic<double> m_currentFPS{0.0};
    
    // Frame data cache for unified access
    mutable std::mutex m_frameCacheMutex;
    std::unordered_map<size_t, UnifiedFrameData> m_frameCache;
    static constexpr size_t MAX_CACHE_SIZE = 1000;
};
