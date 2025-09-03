#include "recording_data_loader.h"

#include <QMetaObject>
#include <QString>

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>
#include <cmath>

// Utility function implementations
QImage cvMatToQImage(const cv::Mat &mat) {
    if (mat.empty()) return {};
    cv::Mat rgb;
    switch (mat.type()) {
        case CV_8UC1:
            cv::cvtColor(mat, rgb, cv::COLOR_GRAY2RGBA);
            break;
        case CV_8UC3:
            cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGBA);
            break;
        case CV_8UC4:
            rgb = mat.clone();
            break;
        default:
            return {};
    }
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGBA8888).copy();
}

long long extract_frame_index(const std::string &pathStr) {
    auto filenamePos = pathStr.find_last_of("/\\");
    std::string name = (filenamePos==std::string::npos)? pathStr : pathStr.substr(filenamePos+1);
    auto dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos) name = name.substr(0, dotPos);
    // Find last contiguous digit run
    int i = static_cast<int>(name.size()) - 1;
    while (i >= 0 && !std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    if (i < 0) return -1;
    int end = i;
    while (i >= 0 && std::isdigit(static_cast<unsigned char>(name[i]))) --i;
    std::string num = name.substr(i + 1, end - i);
    try { return std::stoll(num); } catch (...) { return -1; }
}

// EventCameraLoader implementation
EventCameraLoader::EventCameraLoader(const std::string &filePath) 
    : m_filePath(filePath) {
    initialize();
    
    // Start prefetch thread
    m_prefetchThread = std::thread(&EventCameraLoader::prefetchThreadMain, this);
}

EventCameraLoader::~EventCameraLoader() {
    // Stop prefetch thread
    m_stopPrefetch = true;
    m_prefetchCv.notify_all();
    if (m_prefetchThread.joinable()) {
        m_prefetchThread.join();
    }
}

void EventCameraLoader::initialize() {
    try {
        // Use Stream API for efficient file access
        Metavision::Camera camera = Metavision::Camera::from_file(m_filePath);
        
        // Get camera geometry
        auto &geometry = camera.geometry();
        m_width = geometry.get_width();
        m_height = geometry.get_height();
        
        // Estimate frame count based on file duration and assumed fps
        auto duration_us = camera.offline_streaming_control().get_duration();
        if (duration_us > 0) {
            // Assume 30 fps for estimation
            m_estimatedFrameCount = static_cast<size_t>(std::ceil(duration_us / 33333.0)); // 33.333ms per frame
        } else {
            m_estimatedFrameCount = 1000; // fallback estimate
        }
        
        m_isValid = true;
        std::cout << "EventCameraLoader initialized: " << m_width << "x" << m_height 
                  << ", estimated frames: " << m_estimatedFrameCount 
                  << ", duration: " << duration_us << " us" << std::endl;
                  
    } catch (const std::exception &e) {
        std::cout << "Failed to initialize EventCameraLoader: " << e.what() << std::endl;
        m_isValid = false;
    }
}

QImage EventCameraLoader::getFrame(size_t frameIndex, double fps) {
    if (!m_isValid) {
        return QImage(m_width > 0 ? m_width : 640, m_height > 0 ? m_height : 480, QImage::Format_RGBA8888);
    }
    
    std::lock_guard<std::mutex> lock(m_frameMutex);
    
    // Check frame cache first
    auto it = m_frameCache.find(frameIndex);
    if (it != m_frameCache.end()) {
        return it->second;
    }
    
    // Calculate time range for this frame
    Metavision::timestamp frameDuration = static_cast<Metavision::timestamp>(1000000.0 / fps); // microseconds
    Metavision::timestamp frameStartTime = frameIndex * frameDuration;
    Metavision::timestamp frameEndTime = frameStartTime + frameDuration;
    
    // Generate frame on-demand using streaming approach (no pre-loading)
    QImage frame = generateFrameFromTimeRange(frameStartTime, frameEndTime);
    
    // Cache the frame
    m_frameCache[frameIndex] = frame;
    
    // Limit cache size to prevent memory bloat
    if (m_frameCache.size() > MAX_CACHE_SIZE) {
        auto oldest = m_frameCache.begin();
        for (auto it = m_frameCache.begin(); it != m_frameCache.end(); ++it) {
            if (it->first < oldest->first) {
                oldest = it;
            }
        }
        m_frameCache.erase(oldest);
    }
    
    return frame;
}

QImage EventCameraLoader::generateFrameFromTimeRange(Metavision::timestamp startTime, Metavision::timestamp endTime) {
    try {
        // Use a streaming approach: create a temporary camera instance for this frame
        Metavision::Camera camera = Metavision::Camera::from_file(m_filePath);
        
        // Seek to the start time
        camera.offline_streaming_control().seek(startTime);
        
        // Collect events in the time range
        std::vector<Metavision::EventCD> frameEvents;
        frameEvents.reserve(100000); // Reserve reasonable space
        
        auto callbackId = camera.cd().add_callback([&frameEvents, startTime, endTime](const Metavision::EventCD *begin, const Metavision::EventCD *end) {
            for (auto it = begin; it != end; ++it) {
                if (it->t >= startTime && it->t < endTime) {
                    frameEvents.push_back(*it);
                } else if (it->t >= endTime) {
                    // We've gone past our time window, stop collecting
                    break;
                }
            }
        });
        
        // Start the camera and process until we reach our end time
        camera.start();
        
        auto processingStart = std::chrono::high_resolution_clock::now();
        const auto maxProcessingTime = std::chrono::milliseconds(200); // Quick timeout
        
        while (camera.is_running()) {
            auto now = std::chrono::high_resolution_clock::now();
            if (now - processingStart > maxProcessingTime) {
                break;
            }
            
            // Check if we've processed past our end time
            if (camera.get_last_timestamp() >= endTime) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        camera.stop();
        camera.cd().remove_callback(callbackId);
        
        // Generate frame from collected events
        return generateFrameFromEvents(frameEvents);
        
    } catch (const std::exception &e) {
        std::cout << "Error generating frame for time " << startTime << ": " << e.what() << std::endl;
    }
    
    // Return error frame
    QImage errorFrame(m_width, m_height, QImage::Format_RGBA8888);
    errorFrame.fill(Qt::darkGray);
    return errorFrame;
}

QImage EventCameraLoader::generateFrameFromEvents(const std::vector<Metavision::EventCD> &events) {
    // Create accumulation frame
    cv::Mat frame = cv::Mat::zeros(m_height, m_width, CV_8UC3);
    
    // Background color (dark gray)
    frame.setTo(cv::Scalar(64, 64, 64));
    
    // Accumulate events with simple visualization
    for (const auto &event : events) {
        if (event.x >= 0 && event.x < m_width && event.y >= 0 && event.y < m_height) {
            cv::Vec3b &pixel = frame.at<cv::Vec3b>(event.y, event.x);
            if (event.p == 1) { // Positive event (white)
                pixel[0] = 255; pixel[1] = 255; pixel[2] = 255;
            } else { // Negative event (blue)
                pixel[0] = 255; pixel[1] = 0; pixel[2] = 0; // BGR format: Blue=255, Green=0, Red=0
            }
        }
    }
    
    return cvMatToQImage(frame);
}

QSet<int> EventCameraLoader::getCachedFrames() const {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    QSet<int> cachedIndices;
    
    for (const auto &pair : m_frameCache) {
        cachedIndices.insert(static_cast<int>(pair.first));
    }
    
    return cachedIndices;
}

void EventCameraLoader::setCurrentFrameIndex(size_t frameIndex) {
    size_t oldFrame = m_currentFrameIndex.exchange(frameIndex);
    
    // If we jumped significantly (more than a few frames), interrupt current prefetching
    if (abs(static_cast<long long>(frameIndex) - static_cast<long long>(oldFrame)) > 10) {
        // Signal the prefetch thread to restart from new position
        {
            std::lock_guard<std::mutex> lock(m_prefetchMutex);
            m_prefetchRestart = true;
            m_prefetchDirty = true;
        }
        m_prefetchCv.notify_one();
    } else {
        // Small change, just update normally
        requestPrefetch();
    }
}

void EventCameraLoader::setPlaybackFps(double fps) {
    m_fps = fps;
}

void EventCameraLoader::requestPrefetch() {
    {
        std::lock_guard<std::mutex> lock(m_prefetchMutex);
        m_prefetchDirty = true;
    }
    m_prefetchCv.notify_one();
}

void EventCameraLoader::prefetchThreadMain() {
    while (!m_stopPrefetch) {
        std::unique_lock<std::mutex> lock(m_prefetchMutex);
        
        // Wait for prefetch request or stop signal
        m_prefetchCv.wait(lock, [this] { return m_prefetchDirty || m_stopPrefetch; });
        
        if (m_stopPrefetch) break;
        
        bool shouldRestart = m_prefetchRestart;
        m_prefetchDirty = false;
        m_prefetchRestart = false;
        lock.unlock();
        
        // Get current frame index
        size_t currentFrame = m_currentFrameIndex.load();
        double fps = m_fps.load();
        
        // If restarting, we might want to clear some cache entries that are far from current position
        if (shouldRestart) {
            std::lock_guard<std::mutex> cacheLock(m_frameMutex);
            // Remove cache entries that are too far from current position to make room
            auto it = m_frameCache.begin();
            while (it != m_frameCache.end()) {
                size_t cachedFrame = it->first;
                // Keep frames within a reasonable range around current position
                if (cachedFrame < currentFrame && (currentFrame - cachedFrame) > PREFETCH_AHEAD_FRAMES) {
                    it = m_frameCache.erase(it);
                } else if (cachedFrame > currentFrame && (cachedFrame - currentFrame) > PREFETCH_AHEAD_FRAMES * 2) {
                    it = m_frameCache.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Prefetch ahead frames
        for (size_t i = 1; i <= PREFETCH_AHEAD_FRAMES; ++i) {
            if (m_stopPrefetch) break;
            
            // Check if we got a restart request during prefetching
            {
                std::lock_guard<std::mutex> restartLock(m_prefetchMutex);
                if (m_prefetchRestart) {
                    // Restart requested, break out of current prefetch loop
                    break;
                }
            }
            
            size_t frameIndex = currentFrame + i;
            if (frameIndex >= m_estimatedFrameCount) break;
            
            // Check if frame is already cached
            {
                std::lock_guard<std::mutex> cacheLock(m_frameMutex);
                if (m_frameCache.find(frameIndex) != m_frameCache.end()) {
                    continue; // Already cached
                }
            }
            
            // Generate frame in background
            try {
                QImage frame = generateFrameFromTimeRange(
                    frameIndex * static_cast<Metavision::timestamp>(1000000.0 / fps),
                    (frameIndex + 1) * static_cast<Metavision::timestamp>(1000000.0 / fps)
                );
                
                // Cache the frame
                {
                    std::lock_guard<std::mutex> cacheLock(m_frameMutex);
                    if (m_frameCache.size() < MAX_CACHE_SIZE) {
                        m_frameCache[frameIndex] = frame;
                    }
                }
                
                // Small delay to prevent overwhelming the system
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
            } catch (const std::exception &e) {
                std::cout << "Prefetch error for frame " << frameIndex << ": " << e.what() << std::endl;
                break; // Stop prefetching on error
            }
        }
    }
}

// RecordingDataLoader implementation
RecordingDataLoader::RecordingDataLoader(QObject *parent) : QObject(parent) {
}

RecordingDataLoader::~RecordingDataLoader() {
    abortLoading();
}

void RecordingDataLoader::loadRecording(const std::string &dirPath) {
    // Abort any existing loading
    abortLoading();
    
    // Reset state
    m_data = RecordingData{};
    m_dataReady = false;
    m_abortLoading = false;
    m_loading = true;
    
    emit loadingStarted(QString::fromStdString(dirPath));
    
    // Start loading in background thread
    m_loaderThread = std::thread([this, dirPath]() {
        this->loadDataWorker(dirPath);
    });
}

void RecordingDataLoader::abortLoading() {
    m_abortLoading = true;
    if (m_loaderThread.joinable()) {
        m_loaderThread.join();
    }
    m_loading = false;
}

cv::Mat RecordingDataLoader::getFrameCameraFrame(int camera, size_t frameIndex) const {
    if (!m_dataReady.load() || camera < 0 || camera >= static_cast<int>(m_data.frameCams.size())) {
        return {};
    }
    return m_data.frameCams[camera].loadFrame(frameIndex);
}

QImage RecordingDataLoader::getEventCameraFrame(int camera, size_t frameIndex) const {
    if (!m_dataReady.load() || camera < 0 || camera >= static_cast<int>(m_data.eventCams.size())) {
        return {};
    }
    const auto &eventCam = m_data.eventCams[camera];
    if (!eventCam.loader || !eventCam.isValid) {
        return {};
    }
    
    // Use lazy loading - generate frame on demand
    return eventCam.loader->getFrame(frameIndex);
}

QSet<int> RecordingDataLoader::getCachedEventFrames(int camera) const {
    if (!m_dataReady.load() || camera < 0 || camera >= static_cast<int>(m_data.eventCams.size())) {
        return {};
    }
    const auto &eventCam = m_data.eventCams[camera];
    if (!eventCam.loader || !eventCam.isValid) {
        return {};
    }
    return eventCam.loader->getCachedFrames();
}

QSet<int> RecordingDataLoader::getAllCachedFrames() const {
    QSet<int> allCached;
    
    // Only show event camera cached frames since that's where the expensive processing happens
    // Frame cameras are just file reads and don't need caching visualization
    for (size_t cam = 0; cam < m_data.eventCams.size(); ++cam) {
        QSet<int> eventCached = getCachedEventFrames(static_cast<int>(cam));
        allCached.unite(eventCached);
    }
    
    return allCached;
}

void RecordingDataLoader::notifyFrameChanged(size_t frameIndex) {
    if (!m_dataReady.load()) return;
    
    // Notify all event camera loaders about the frame change for prefetching
    for (auto &eventCam : m_data.eventCams) {
        if (eventCam.loader && eventCam.isValid) {
            eventCam.loader->setCurrentFrameIndex(frameIndex);
        }
    }
}

void RecordingDataLoader::loadDataWorker(const std::string &dirPath) {
    namespace fs = std::filesystem;
    
    try {
        m_data.loadedPath = dirPath;
        
        // Load frame cameras: frame_cam0, frame_cam1
        emit loadingProgress("Loading frame cameras...");
        m_data.frameCams.resize(2);
        for (int cam = 0; cam < 2; ++cam) {
            if (m_abortLoading) return;
            loadFrameCameraData(dirPath, cam, m_data.frameCams[cam]);
        }

        // Load event cameras: ebv_cam_0.*, ebv_cam_1.* (raw/hdf5)
        emit loadingProgress("Loading event cameras...");
        m_data.eventCams.resize(2);
        for (int cam = 0; cam < 2; ++cam) {
            if (m_abortLoading) return;
            loadEventCameraData(dirPath, cam, m_data.eventCams[cam]);
        }

        // Calculate total frames
        m_data.totalFrames = calculateTotalFrames();
        m_data.isValid = true;

        // Notify completion on main thread
        QMetaObject::invokeMethod(this, [this, dirPath]() {
            m_dataReady = true;
            m_loading = false;
            emit loadingFinished(true, QString("Successfully loaded: %1").arg(QString::fromStdString(dirPath)));
        }, Qt::QueuedConnection);

    } catch (const std::exception &e) {
        QMetaObject::invokeMethod(this, [this, error = std::string(e.what())]() {
            m_dataReady = false;
            m_loading = false;
            m_data.isValid = false;
            emit loadingFinished(false, QString("Failed to load: %1").arg(QString::fromStdString(error)));
        }, Qt::QueuedConnection);
    } catch (...) {
        QMetaObject::invokeMethod(this, [this]() {
            m_dataReady = false;
            m_loading = false;
            m_data.isValid = false;
            emit loadingFinished(false, "Failed to load: Unknown error");
        }, Qt::QueuedConnection);
    }
}

void RecordingDataLoader::loadFrameCameraData(const std::string &dirPath, int camera, FrameCameraData &data) {
    namespace fs = std::filesystem;
    
    fs::path camDir = fs::path(dirPath) / ("frame_cam" + std::to_string(camera));
    if (fs::exists(camDir) && fs::is_directory(camDir)) {
        for (auto &entry : fs::directory_iterator(camDir)) {
            if (m_abortLoading) return;
            
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                    data.image_files.push_back(entry.path().string());
                }
            }
        }
        
        // Sort by frame index extracted from filename
        std::sort(data.image_files.begin(), data.image_files.end(), 
                  [](const std::string &a, const std::string &b) {
            long long ia = extract_frame_index(a);
            long long ib = extract_frame_index(b);
            if (ia == -1 || ib == -1) return a < b; // fallback
            if (ia != ib) return ia < ib;
            return a < b; // stable tie-break
        });
    }

    // Debug output
    for (const auto &fname : data.image_files) {
        std::cout << "FrameCam" << camera << ": " << fname << std::endl;
    }
}

void RecordingDataLoader::loadEventCameraData(const std::string &dirPath, int camera, EventCameraData &data) {
    namespace fs = std::filesystem;
    
    fs::path fileH5 = fs::path(dirPath) / ("ebv_cam_" + std::to_string(camera) + ".hdf5");
    fs::path fileRaw = fs::path(dirPath) / ("ebv_cam_" + std::to_string(camera) + ".raw");
    fs::path useFile;
    
    if (fs::exists(fileH5)) {
        useFile = fileH5;
        std::cout << "Found HDF5 file for camera " << camera << ": " << useFile << std::endl;
    } else if (fs::exists(fileRaw)) {
        useFile = fileRaw;
        std::cout << "Found RAW file for camera " << camera << ": " << useFile << std::endl;
    }
    
    if (!useFile.empty()) {
        // Initialize lazy loader instead of pre-generating all frames
        data.filePath = useFile.string();
        data.loader = std::make_unique<EventCameraLoader>(useFile.string());
        
        if (data.loader->isValid()) {
            data.width = data.loader->getWidth();
            data.height = data.loader->getHeight();
            data.estimatedFrameCount = data.loader->getEstimatedFrameCount();
            data.isValid = true;
            
            std::cout << "EventCamera " << camera << " loaded: " 
                      << data.width << "x" << data.height 
                      << ", estimated frames: " << data.estimatedFrameCount << std::endl;
        } else {
            std::cout << "Failed to load event camera " << camera << std::endl;
            data.isValid = false;
        }
    } else {
        std::cout << "No event data file found for camera " << camera << std::endl;
        data.isValid = false;
    }
}

size_t RecordingDataLoader::calculateTotalFrames() const {
    size_t maxFrameCount = 0;
    for (const auto &f : m_data.frameCams) {
        maxFrameCount = std::max(maxFrameCount, f.image_files.size());
    }
    
    size_t maxEventCount = 0;
    for (const auto &e : m_data.eventCams) {
        if (e.isValid) {
            maxEventCount = std::max(maxEventCount, e.estimatedFrameCount);
        }
    }
    
    size_t total = std::max(maxFrameCount, maxEventCount);
    return (total == 0) ? 1 : total;
}
