#include "recording_data_loader.h"

#include <QMetaObject>
#include <QString>

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>

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
    if (frameIndex >= eventCam.frames.size()) {
        return {};
    }
    return eventCam.frames[frameIndex];
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
    } else if (fs::exists(fileRaw)) {
        useFile = fileRaw;
    }
    
    if (!useFile.empty()) {
        // Load events and build accumulation frames
        Metavision::Camera camObj;
        try {
            camObj = Metavision::Camera::from_file(useFile.string());
            auto &geometry = camObj.geometry();
            int width = geometry.get_width();
            int height = geometry.get_height();
            
            Metavision::CDFrameGenerator generator(width, height);
            generator.set_display_accumulation_time_us(33333); // ~30 fps
            std::mutex gMutex;
            
            generator.start(30, [&data, &gMutex, this](const Metavision::timestamp &ts, const cv::Mat &frame) {
                if (m_abortLoading) return;
                
                cv::Mat gray;
                if (frame.channels() == 1) {
                    gray = frame;
                } else {
                    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
                }
                
                std::lock_guard<std::mutex> lock(gMutex);
                data.frames.push_back(cvMatToQImage(gray));
            });
            
            camObj.cd().add_callback([&generator, &gMutex](const Metavision::EventCD *begin, const Metavision::EventCD *end) {
                std::lock_guard<std::mutex> lock(gMutex);
                generator.add_events(begin, end);
            });
            
            camObj.start();
            
            // Poll until end of file
            while (!m_abortLoading) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                if (!camObj.is_running()) break; // if API available
            }
            
            camObj.stop();
            generator.stop();
            
        } catch (const std::exception &e) {
            // Create error frame
            QImage errImg(400, 200, QImage::Format_RGBA8888);
            errImg.fill(Qt::black);
            data.frames.push_back(errImg);
            std::cout << "Error loading event camera " << camera << ": " << e.what() << std::endl;
        }
    }
}

size_t RecordingDataLoader::calculateTotalFrames() const {
    size_t maxFrameCount = 0;
    for (const auto &f : m_data.frameCams) {
        maxFrameCount = std::max(maxFrameCount, f.image_files.size());
    }
    
    size_t maxEventCount = 0;
    for (const auto &e : m_data.eventCams) {
        maxEventCount = std::max(maxEventCount, e.frames.size());
    }
    
    size_t total = std::max(maxFrameCount, maxEventCount);
    return (total == 0) ? 1 : total;
}
