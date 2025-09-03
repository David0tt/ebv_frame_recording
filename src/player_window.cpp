#include "player_window.h"

#include <QApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <QSpacerItem>
#include <QSizePolicy>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QPixmap>
#include <QColor>
#include <QPalette>
#include <QMetaObject>

#include <metavision/sdk/stream/camera.h>
#include <metavision/sdk/core/utils/cd_frame_generator.h>

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <chrono>
#include <thread>

// Utility function implementations
Pane createPane(const QString &title, const QColor &color) {
    Pane p;
    p.frame = new QFrame;
    p.frame->setFrameShape(QFrame::StyledPanel);
    p.frame->setLineWidth(1);
    p.frame->setAutoFillBackground(true);
    QPalette pal = p.frame->palette();
    QColor bg = color.lighter(170);
    bg.setAlpha(40);
    pal.setColor(QPalette::Window, bg);
    p.frame->setPalette(pal);

    auto *layout = new QVBoxLayout(p.frame);
    auto *labelTitle = new QLabel("<b>" + title + "</b>");
    p.content = new QLabel("(image/event view placeholder)");
    p.content->setAlignment(Qt::AlignCenter);
    layout->addWidget(labelTitle);
    layout->addWidget(p.content, 1);
    return p;
}

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

// PlayerWindow implementation
PlayerWindow::PlayerWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("EBV Multi-Camera Player Mockup");
    resize(1400, 900);

    auto *rootLayout = new QVBoxLayout(this);

    // Top bar with Open button + path label
    auto *topBar = new QHBoxLayout();
    m_openButton = new QPushButton(tr("Open Folder…"));
    m_pathLabel = new QLabel(tr("No folder loaded"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    topBar->addWidget(m_openButton);
    topBar->addSpacing(12);
    topBar->addWidget(m_pathLabel, 1);
    rootLayout->addLayout(topBar);

    // Grid
    auto *grid = new QGridLayout();
    grid->setSpacing(4);
    auto frameLeft  = createPane("Frame Camera Left", QColor(70,120,200));
    auto frameRight = createPane("Frame Camera Right", QColor(70,120,200));
    auto eventLeft  = createPane("Event Camera Left", QColor(200,140,70));
    auto eventRight = createPane("Event Camera Right", QColor(200,140,70));
    m_panes = {frameLeft, frameRight, eventLeft, eventRight};
    grid->addWidget(frameLeft.frame, 0, 0);
    grid->addWidget(frameRight.frame, 0, 1);
    grid->addWidget(eventLeft.frame, 1, 0);
    grid->addWidget(eventRight.frame, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    rootLayout->addLayout(grid, 1);

    // Timeline slider
    m_timelineSlider = new QSlider(Qt::Horizontal);
    m_timelineSlider->setRange(0, 1000);
    m_timelineSlider->setSingleStep(1);
    m_timelineSlider->setPageStep(25);
    rootLayout->addWidget(m_timelineSlider);

    // Transport controls + status (buttons centered, status bottom-right)
    auto *controlsLayout = new QHBoxLayout();
    m_btnBack = new QPushButton("<<");
    m_btnPlay = new QPushButton("Play");
    m_btnFwd = new QPushButton(">>");
    m_statusLabel = new QLabel("Frame 0 / 0    00:00.000 / 00:00.000");
    QFont mono = m_statusLabel->font();
    mono.setFamily("Monospace");
    m_statusLabel->setFont(mono);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    // Left stretch
    controlsLayout->addStretch(1);
    // Center button cluster
    auto *buttonCluster = new QHBoxLayout();
    buttonCluster->setSpacing(8);
    buttonCluster->addWidget(m_btnBack);
    buttonCluster->addWidget(m_btnPlay);
    buttonCluster->addWidget(m_btnFwd);
    controlsLayout->addLayout(buttonCluster);
    // Right stretch then status label
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_statusLabel, 0, Qt::AlignRight);
    rootLayout->addLayout(controlsLayout);

    // Timer for mock playback
    m_timer.setInterval(30);
    connect(&m_timer, &QTimer::timeout, this, [this]{
        int v = m_timelineSlider->value();
        if (v < m_timelineSlider->maximum()) {
            m_timelineSlider->setValue(v + 1);
        } else {
            m_timer.stop();
            m_btnPlay->setText("Play");
        }
    });

    connect(m_btnPlay, &QPushButton::clicked, this, [this]{
        if (m_timer.isActive()) { m_timer.stop(); m_btnPlay->setText("Play"); }
        else { m_timer.start(); m_btnPlay->setText("Pause"); }
    });
    connect(m_btnBack, &QPushButton::clicked, this, [this]{
        int v = m_timelineSlider->value();
        m_timelineSlider->setValue(std::max(0, v - 50));
    });
    connect(m_btnFwd, &QPushButton::clicked, this, [this]{
        int v = m_timelineSlider->value();
        m_timelineSlider->setValue(std::min(m_timelineSlider->maximum(), v + 50));
    });

    connect(m_openButton, &QPushButton::clicked, this, [this]{ selectAndLoadFolder(); });

    connect(m_timelineSlider, &QSlider::valueChanged, this, [this](int v){
        if (!m_dataReady.load()) return;
        m_currentIndex = v;
        updateDisplays();
        updateStatus();
    });
}

PlayerWindow::~PlayerWindow() {
    m_abortLoading = true;
    if (m_loaderThread.joinable()) m_loaderThread.join();
}

void PlayerWindow::selectAndLoadFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Recording Folder"), QDir::homePath());
    if (!dir.isEmpty()) {
        loadRecording(dir);
    }
}

void PlayerWindow::loadRecording(const QString &dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Folder Missing"), tr("Directory does not exist:\n%1").arg(dirPath));
        return;
    }
    if (m_loaderThread.joinable()) {
        m_abortLoading = true;
        m_loaderThread.join();
    }
    m_loadedDir = dirPath;
    m_pathLabel->setText(tr("Loading: %1 ...").arg(dirPath));
    m_dataReady = false;
    m_abortLoading = false;
    m_frameCams.clear();
    m_eventCams.clear();
    m_currentIndex = 0;
    m_timelineSlider->setValue(0);

    // Clear panes
    for (auto &p : m_panes) p.content->setText("Loading...");

    m_loaderThread = std::thread([this, path = dirPath.toStdString()](){ this->loadDataWorker(path); });
    updateStatus();
}

void PlayerWindow::autoLoadIfProvided(const QString &dirPath) {
    if (!dirPath.isEmpty()) {
        loadRecording(dirPath);
    }
}

void PlayerWindow::loadDataWorker(const std::string &dirPath) {
    namespace fs = std::filesystem;
    try {
        // Frame cameras: frame_cam0, frame_cam1
        for (int cam = 0; cam < 2; ++cam) {
            FrameCameraData fcd;
            fs::path camDir = fs::path(dirPath) / ("frame_cam" + std::to_string(cam));
            if (fs::exists(camDir) && fs::is_directory(camDir)) {
                for (auto &entry : fs::directory_iterator(camDir)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                            fcd.image_files.push_back(entry.path().string());
                        }
                    }
                }
                // TODO hacky solution to sort by number in file name
                std::sort(fcd.image_files.begin(), fcd.image_files.end(), [](const std::string &a, const std::string &b){
                    long long ia = extract_frame_index(a);
                    long long ib = extract_frame_index(b);
                    if (ia == -1 || ib == -1) return a < b; // fallback
                    if (ia != ib) return ia < ib;
                    return a < b; // stable tie-break
                });
            }

            // Debug
            for (const auto &fname : fcd.image_files) {
                std::cout << "FrameCam" << cam << ":" << fname << std::endl;
            }

            m_frameCams.push_back(std::move(fcd));
        }

        // Event cameras: ebv_cam_0.*, ebv_cam_1.* (raw/hdf5)
        for (int cam = 0; cam < 2; ++cam) {
            EventCameraData ecd;
            fs::path fileH5 = fs::path(dirPath) / ("ebv_cam_" + std::to_string(cam) + ".hdf5");
            fs::path fileRaw = fs::path(dirPath) / ("ebv_cam_" + std::to_string(cam) + ".raw");
            fs::path useFile;
            if (fs::exists(fileH5)) useFile = fileH5; else if (fs::exists(fileRaw)) useFile = fileRaw;
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
                    generator.start(30, [&ecd,&gMutex,this](const Metavision::timestamp &ts, const cv::Mat &frame){
                        if (m_abortLoading) return;
                        cv::Mat gray;
                        if (frame.channels()==1) gray = frame; else cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
                        ecd.frames.push_back(cvMatToQImage(gray));
                    });
                    camObj.cd().add_callback([&generator,&gMutex](const Metavision::EventCD *begin, const Metavision::EventCD *end){
                        std::lock_guard<std::mutex> lock(gMutex);
                        generator.add_events(begin, end);
                    });
                    camObj.start();
                    // Poll until end of file
                    while (!m_abortLoading) {
                        // Heuristic: break when camera has no more data (from_file stops producing events)
                        // We can't directly query so limit maximum loop iterations without new frames.
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        if (!camObj.is_running()) break; // if API available
                    }
                    camObj.stop();
                    generator.stop();
                } catch (const std::exception &e) {
                    // Leave ecd empty but note error frame
                    QImage errImg(400,200,QImage::Format_RGBA8888);
                    errImg.fill(Qt::black);
                    ecd.frames.push_back(errImg);
                }
            }
            m_eventCams.push_back(std::move(ecd));
        }

        // Compute timeline length
        size_t maxFrameCount = 0;
        for (auto &f : m_frameCams) maxFrameCount = std::max(maxFrameCount, f.image_files.size());
        size_t maxEventCount = 0;
        for (auto &e : m_eventCams) maxEventCount = std::max(maxEventCount, e.frames.size());
        size_t total = std::max(maxFrameCount, maxEventCount);
        if (total == 0) total = 1;
        m_totalFrames = total;

        QMetaObject::invokeMethod(this, [this, dirPath]{
            m_pathLabel->setText(tr("Loaded: %1").arg(QString::fromStdString(dirPath)));
            m_timelineSlider->setRange(0, static_cast<int>(m_totalFrames-1));
            m_dataReady = true;
            updateDisplays();
            updateStatus();
        }, Qt::QueuedConnection);
    } catch (...) {
        QMetaObject::invokeMethod(this, [this]{
            m_pathLabel->setText("Failed to load");
            for (auto &p : m_panes) p.content->setText("Load failed");
            updateStatus();
        }, Qt::QueuedConnection);
    }
}

void PlayerWindow::updateDisplays() {
    if (!m_dataReady.load()) return;
    size_t idx = m_currentIndex;
    // Frame cameras
    for (int cam=0; cam<2; ++cam) {
        if (cam < (int)m_frameCams.size() && idx < m_frameCams[cam].image_files.size()) {
            cv::Mat img = m_frameCams[cam].loadFrame(idx);
            if (!img.empty()) {
                QPixmap pm = QPixmap::fromImage(cvMatToQImage(img)).scaled(m_panes[cam].content->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                m_panes[cam].content->setPixmap(pm);
            } else m_panes[cam].content->setText("(no frame)");
        } else m_panes[cam].content->setText("(no frame)");
    }
    // Event cameras
    for (int cam=0; cam<2; ++cam) {
        int paneIndex = 2 + cam; // bottom row
        if (cam < (int)m_eventCams.size() && idx < m_eventCams[cam].frames.size() && !m_eventCams[cam].frames[idx].isNull()) {
            QPixmap pm = QPixmap::fromImage(m_eventCams[cam].frames[idx]).scaled(m_panes[paneIndex].content->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_panes[paneIndex].content->setPixmap(pm);
        } else {
            m_panes[paneIndex].content->setText("(no events)");
        }
    }
}

void PlayerWindow::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updateDisplays();
    updateStatus();
}

QString PlayerWindow::formatTime(double seconds) const {
    if (seconds < 0) seconds = 0;
    int ms = static_cast<int>(std::llround(seconds * 1000.0));
    int h = ms / 3600000; ms %= 3600000;
    int m = ms / 60000;   ms %= 60000;
    int s = ms / 1000;    ms %= 1000;
    if (h>0)
        return QString::asprintf("%d:%02d:%02d.%03d", h, m, s, ms);
    return QString::asprintf("%02d:%02d.%03d", m, s, ms);
}

void PlayerWindow::updateStatus() {
    size_t total = m_totalFrames;
    size_t cur = std::min<size_t>(m_currentIndex.load(), total? total-1:0);
    double curTime = cur / m_assumedFps;
    double totTime = (total>0? (total-1)/m_assumedFps:0.0);
    if (m_statusLabel) {
        m_statusLabel->setText(QString("Frame %1 / %2    %3 / %4")
            .arg(cur).arg(total? total-1:0)
            .arg(formatTime(curTime))
            .arg(formatTime(totTime)));
    }
}
