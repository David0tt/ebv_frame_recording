#include "recording_buffer.h"
#include "recording_loader.h"
#include "utils_qt.h"
// Note: RecordingManager is included here to access its methods, but not in header
#include "recording_manager.h"
#include <iostream>
#include <algorithm>

RecordingBuffer::RecordingBuffer(QObject *parent) 
    : QObject(parent)
{
}

RecordingBuffer::~RecordingBuffer() {
    stop();
}

void RecordingBuffer::setPlaybackMode(RecordingLoader* dataLoader) {
    if (m_active) {
        stop();
    }
    
    m_dataLoader = dataLoader;
    m_recordingManager = nullptr;
    m_currentMode = Mode::Playback;
    
    if (m_dataLoader) {
        setupPlaybackMode();
        m_active = true;
        emit modeChanged(Mode::Playback);
    }
}

void RecordingBuffer::setLiveMode(void* recordingManager) {
    if (m_active) {
        stop();
    }
    
    m_recordingManager = recordingManager;
    m_dataLoader = nullptr;
    m_currentMode = Mode::Live;
    
    RecordingManager* manager = static_cast<RecordingManager*>(m_recordingManager);
    if (manager && manager->isRecording()) {
        startLiveBuffering();
        m_active = true;
        emit modeChanged(Mode::Live);
    }
}

void RecordingBuffer::stop() {
    if (!m_active) {
        return;
    }
    
    m_active = false;
    
    if (m_currentMode == Mode::Live) {
        stopLiveBuffering();
    }
    
    // Clear caches
    {
        std::lock_guard<std::mutex> lock(m_frameCacheMutex);
        m_frameCache.clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(m_liveBufferMutex);
        while (!m_liveFrameBuffer.empty()) m_liveFrameBuffer.pop();
        while (!m_liveEventBuffer.empty()) m_liveEventBuffer.pop();
    }
    
    m_currentFrameIndex = 0;
    m_currentFPS = 0.0;
}

cv::Mat RecordingBuffer::getFrameCameraFrame(int camera, size_t frameIndex) const {
    if (!m_active) {
        return cv::Mat();
    }
    
    if (m_currentMode == Mode::Playback && m_dataLoader) {
        return m_dataLoader->getFrameCameraFrame(camera, frameIndex);
    } else if (m_currentMode == Mode::Live) {
        // For live mode, get the most recent frame for this camera
        std::lock_guard<std::mutex> lock(m_liveBufferMutex);
        
        // Search backwards through the buffer for the most recent frame from this camera
        auto tempQueue = m_liveFrameBuffer;
        BufferedFrameData latestFrame;
        bool found = false;
        
        while (!tempQueue.empty()) {
            auto frame = tempQueue.front();
            tempQueue.pop();
            if (frame.cameraId == camera && frame.isValid) {
                latestFrame = frame;
                found = true;
            }
        }
        
        return found ? latestFrame.image : cv::Mat();
    }
    
    return cv::Mat();
}

QImage RecordingBuffer::getEventCameraFrame(int camera, size_t frameIndex) const {
    if (!m_active) {
        return QImage();
    }
    
    if (m_currentMode == Mode::Playback && m_dataLoader) {
        return m_dataLoader->getEventCameraFrame(camera, frameIndex);
    } else if (m_currentMode == Mode::Live) {
        // For live mode, get the most recent event frame for this camera
        std::lock_guard<std::mutex> lock(m_liveBufferMutex);
        
        // Search backwards through the buffer for the most recent frame from this camera
        auto tempQueue = m_liveEventBuffer;
        BufferedEventData latestFrame;
        bool found = false;
        
        while (!tempQueue.empty()) {
            auto frame = tempQueue.front();
            tempQueue.pop();
            if (frame.cameraId == camera && frame.isValid) {
                latestFrame = frame;
                found = true;
            }
        }
        
        return found ? latestFrame.frame : QImage();
    }
    
    return QImage();
}

UnifiedFrameData RecordingBuffer::getCurrentFrameData() const {
    std::lock_guard<std::mutex> lock(m_currentDataMutex);
    return m_currentFrameData;
}

size_t RecordingBuffer::getTotalFrames() const {
    if (m_currentMode == Mode::Playback && m_dataLoader && m_dataLoader->isDataReady()) {
        return m_dataLoader->getData().totalFrames;
    }
    return 0;
}

void RecordingBuffer::setCurrentFrameIndex(size_t frameIndex) {
    if (m_currentMode != Mode::Playback) {
        return;
    }
    
    m_currentFrameIndex = frameIndex;
    
    if (m_dataLoader) {
        m_dataLoader->notifyFrameChanged(frameIndex);
    }
    
    // Update current frame data
    {
        std::lock_guard<std::mutex> lock(m_currentDataMutex);
        m_currentFrameData = createUnifiedFrame(frameIndex, std::chrono::steady_clock::now());
    }
    
    emit frameDataUpdated(frameIndex);
}

size_t RecordingBuffer::getLiveFrameCount() const {
    if (m_currentMode == Mode::Live) {
        return m_currentFrameIndex.load();
    }
    return 0;
}

UnifiedFrameData RecordingBuffer::getLatestLiveData() const {
    if (m_currentMode == Mode::Live) {
        return getCurrentFrameData();
    }
    return UnifiedFrameData();
}

QSet<int> RecordingBuffer::getCachedFrames() const {
    if (m_currentMode == Mode::Playback && m_dataLoader) {
        return m_dataLoader->getAllCachedFrames();
    }
    
    // For live mode, return buffer status
    QSet<int> result;
    if (m_currentMode == Mode::Live) {
        size_t currentFrame = m_currentFrameIndex.load();
        size_t bufferSize = getBufferSize();
        
        // Mark recent frames as "cached"
        for (size_t i = 0; i < bufferSize && i <= currentFrame; ++i) {
            result.insert(static_cast<int>(currentFrame - i));
        }
    }
    
    return result;
}

size_t RecordingBuffer::getBufferSize() const {
    if (m_currentMode == Mode::Live) {
        std::lock_guard<std::mutex> lock(m_liveBufferMutex);
        return std::max(m_liveFrameBuffer.size(), m_liveEventBuffer.size());
    }
    return 0;
}

bool RecordingBuffer::isBufferHealthy() const {
    if (m_currentMode == Mode::Live) {
        size_t bufferSize = getBufferSize();
        return bufferSize >= TARGET_BUFFER_SIZE && bufferSize < MAX_LIVE_BUFFER_SIZE;
    }
    return m_active;
}

void RecordingBuffer::startLiveBuffering() {
    m_stopBuffering = false;
    m_liveBufferThread = std::thread(&RecordingBuffer::liveBufferWorker, this);
}

void RecordingBuffer::stopLiveBuffering() {
    m_stopBuffering = true;
    m_liveBufferCondition.notify_all();
    
    if (m_liveBufferThread.joinable()) {
        m_liveBufferThread.join();
    }
}

void RecordingBuffer::liveBufferWorker() {
    auto lastUpdateTime = std::chrono::steady_clock::now();
    const auto updateInterval = std::chrono::milliseconds(33); // ~30 FPS
    
    RecordingManager* manager = static_cast<RecordingManager*>(m_recordingManager);
    while (!m_stopBuffering && manager && manager->isRecording()) {
        auto now = std::chrono::steady_clock::now();
        
        // Process new data periodically
        if (now - lastUpdateTime >= updateInterval) {
            processLiveFrameData();
            processLiveEventData();
            updateFPS();
            cleanupOldFrames();
            
            // Update current frame data
            {
                std::lock_guard<std::mutex> lock(m_currentDataMutex);
                m_currentFrameData = createUnifiedFrame(m_currentFrameIndex, now);
            }
            
            emit liveDataAvailable(m_currentFrameData);
            emit frameDataUpdated(m_currentFrameIndex);
            
            lastUpdateTime = now;
            m_currentFrameIndex++;
        }
        
        // Check buffer health
        bool healthy = isBufferHealthy();
        emit bufferStatusChanged(healthy);
        
        // Sleep briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void RecordingBuffer::processLiveFrameData() {
    // Get live frame data from RecordingManager
    RecordingManager* manager = static_cast<RecordingManager*>(m_recordingManager);
    if (!manager) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_liveBufferMutex);
    
    // Get frames from both frame cameras
    for (int camera = 0; camera < 2; ++camera) {
        cv::Mat frame;
        size_t frameIndex;
        
        if (manager->getLiveFrameData(camera, frame, frameIndex)) {
            BufferedFrameData frameData;
            frameData.image = frame;
            frameData.cameraId = camera;
            frameData.frameIndex = frameIndex;
            frameData.timestamp = std::chrono::steady_clock::now();
            frameData.isValid = true;
            
            m_liveFrameBuffer.push(frameData);
        }
    }
}

void RecordingBuffer::processLiveEventData() {
    // Get live event data from RecordingManager
    RecordingManager* manager = static_cast<RecordingManager*>(m_recordingManager);
    if (!manager) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_liveBufferMutex);
    
    // Get event frames from both event cameras
    for (int camera = 0; camera < 2; ++camera) {
        cv::Mat eventMat;
        size_t frameIndex;
        
        if (manager->getLiveEventData(camera, eventMat, frameIndex)) {
            BufferedEventData eventData;
            eventData.frame = cvMatToQImage(eventMat); // Convert cv::Mat to QImage
            eventData.cameraId = camera;
            eventData.frameIndex = frameIndex;
            eventData.timestamp = std::chrono::steady_clock::now();
            eventData.isValid = !eventMat.empty();
            
            m_liveEventBuffer.push(eventData);
        }
    }
}

void RecordingBuffer::setupPlaybackMode() {
    if (!m_dataLoader) {
        return;
    }
    
    // Connect to data loader signals if needed
    connect(m_dataLoader, &RecordingLoader::loadingFinished, 
            this, &RecordingBuffer::onRecordingDataReady, Qt::UniqueConnection);
}

void RecordingBuffer::onRecordingDataReady() {
    if (m_currentMode == Mode::Playback && m_dataLoader && m_dataLoader->isDataReady()) {
        // Initialize with frame 0
        setCurrentFrameIndex(0);
    }
}

void RecordingBuffer::updateFPS() {
    std::lock_guard<std::mutex> lock(m_fpsMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFrameTime);
    
    if (timeDiff.count() > 1000) { // Update every second
        size_t currentFrameCount = m_currentFrameIndex;
        size_t frameDiff = currentFrameCount - m_lastFrameCount;
        
        if (timeDiff.count() > 0) {
            double fps = (frameDiff * 1000.0) / timeDiff.count();
            m_currentFPS = fps;
        }
        
        m_lastFrameTime = now;
        m_lastFrameCount = currentFrameCount;
    }
}

void RecordingBuffer::cleanupOldFrames() {
    std::lock_guard<std::mutex> lock(m_liveBufferMutex);
    
    // Remove old frames to keep buffer size manageable
    while (m_liveFrameBuffer.size() > MAX_LIVE_BUFFER_SIZE) {
        m_liveFrameBuffer.pop();
    }
    
    while (m_liveEventBuffer.size() > MAX_LIVE_BUFFER_SIZE) {
        m_liveEventBuffer.pop();
    }
}

UnifiedFrameData RecordingBuffer::createUnifiedFrame(size_t frameIndex, const std::chrono::steady_clock::time_point& timestamp) const {
    UnifiedFrameData unified;
    unified.frameIndex = frameIndex;
    unified.timestamp = timestamp;
    unified.isValid = true;
    
    // Populate frame data (2 frame cameras)
    unified.frameData.resize(2);
    for (int i = 0; i < 2; ++i) {
        unified.frameData[i].image = getFrameCameraFrame(i, frameIndex);
        unified.frameData[i].cameraId = i;
        unified.frameData[i].frameIndex = frameIndex;
        unified.frameData[i].timestamp = timestamp;
        unified.frameData[i].isValid = !unified.frameData[i].image.empty();
    }
    
    // Populate event data (2 event cameras)
    unified.eventData.resize(2);
    for (int i = 0; i < 2; ++i) {
        unified.eventData[i].frame = getEventCameraFrame(i, frameIndex);
        unified.eventData[i].cameraId = i;
        unified.eventData[i].frameIndex = frameIndex;
        unified.eventData[i].timestamp = timestamp;
        unified.eventData[i].isValid = !unified.eventData[i].frame.isNull();
    }
    
    return unified;
}
