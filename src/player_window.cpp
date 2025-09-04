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
#include "recording_manager.h"
#include "recording_loader.h" // for cvMatToQImage utility function

#include <filesystem>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <ctime>

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

// PlayerWindow implementation
PlayerWindow::PlayerWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("EBV Multi-Camera Player Mockup");
    resize(1400, 900);

    // Initialize data loader
    m_dataLoader = new RecordingLoader(this);
    connect(m_dataLoader, &RecordingLoader::loadingStarted, this, &PlayerWindow::onLoadingStarted);
    connect(m_dataLoader, &RecordingLoader::loadingFinished, this, &PlayerWindow::onLoadingFinished);
    connect(m_dataLoader, &RecordingLoader::loadingProgress, this, &PlayerWindow::onLoadingProgress);

    // Initialize recording manager
    m_recordingManager = new RecordingManager();
    m_recordingManager->setStatusCallback([this](const std::string& message) {
        emit QMetaObject::invokeMethod(this, "onRecordingStatusUpdate", 
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString::fromStdString(message)));
    });

    auto *rootLayout = new QVBoxLayout(this);

    // Top bar with Open button + path label + recording controls
    auto *topBar = new QHBoxLayout();
    m_openButton = new QPushButton(tr("Open Folder…"));
    m_pathLabel = new QLabel(tr("No folder loaded"));
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    
    // Recording controls on the right
    m_recordButton = new QPushButton(tr("Start Recording"));
    m_recordButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
    m_recordingStatusLabel = new QLabel("");
    m_recordingStatusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    
    topBar->addWidget(m_openButton);
    topBar->addSpacing(12);
    topBar->addWidget(m_pathLabel, 1);
    topBar->addStretch();
    topBar->addWidget(m_recordingStatusLabel);
    topBar->addSpacing(8);
    topBar->addWidget(m_recordButton);
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
    m_timelineSlider = new CachedTimelineSlider(Qt::Horizontal);
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

    // FPS counter label
    m_fpsLabel = new QLabel("FPS: 0.0");
    m_fpsLabel->setFont(mono);
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_fpsLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    // Left stretch
    controlsLayout->addStretch(1);
    // Center button cluster
    auto *buttonCluster = new QHBoxLayout();
    buttonCluster->setSpacing(8);
    buttonCluster->addWidget(m_btnBack);
    buttonCluster->addWidget(m_btnPlay);
    buttonCluster->addWidget(m_btnFwd);
    controlsLayout->addLayout(buttonCluster);
    // Right stretch then status label and FPS
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_statusLabel, 0, Qt::AlignRight);
    controlsLayout->addSpacing(12);
    controlsLayout->addWidget(m_fpsLabel, 0, Qt::AlignRight);
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

    // Timer for updating cached frame display
    m_cacheUpdateTimer.setInterval(500); // Update every 500ms
    connect(&m_cacheUpdateTimer, &QTimer::timeout, this, &PlayerWindow::updateCachedFrames);
    m_cacheUpdateTimer.start();

    // Initialize FPS tracking
    m_lastFrameTime = std::chrono::steady_clock::now();

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
    connect(m_recordButton, &QPushButton::clicked, this, &PlayerWindow::onRecordingToggle);

    // Recording status timer
    m_recordingTimer.setInterval(1000); // Update every second
    connect(&m_recordingTimer, &QTimer::timeout, this, [this](){
        if (m_isRecording && m_recordingManager) {
            double duration = m_recordingManager->getRecordingDurationSeconds();
            m_recordingStatusLabel->setText(QString("Recording: %1s").arg(duration, 0, 'f', 1));
        }
    });

    connect(m_timelineSlider, &QSlider::valueChanged, this, [this](int v){
        if (!m_dataLoader->isDataReady()) return;
        
        // Update FPS calculation
        updateFPS(v);
        
        m_currentIndex = v;
        
        // Notify event camera loaders about frame change for prefetching
        m_dataLoader->notifyFrameChanged(v);
        
        updateDisplays();
        updateStatus();
    });
}

PlayerWindow::~PlayerWindow() {
    // Stop recording if it's running
    if (m_isRecording) {
        stopRecording();
    }
    // Clean up recording manager
    delete m_recordingManager;
    // Data loader will be cleaned up automatically since it's a child object
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
    
    // Abort any ongoing loading and clean up previous state
    if (m_dataLoader->isLoading()) {
        std::cout << "Aborting previous loading operation..." << std::endl;
        m_dataLoader->abortLoading();
    }
    
    m_loadedDir = dirPath;
    m_currentIndex = 0;
    m_timelineSlider->setValue(0);
    m_timelineSlider->clearCachedFrames(); // Clear cache display for new recording

    // Reset FPS tracking
    m_currentFps = 0.0;
    m_lastFrameIndex = 0;
    m_lastFrameTime = std::chrono::steady_clock::now();
    if (m_fpsLabel) {
        m_fpsLabel->setText("FPS: 0.0");
    }

    // Clear panes
    for (auto &p : m_panes) {
        p.content->setText("Loading...");
    }

    std::cout << "Loading recording from: " << dirPath.toStdString() << std::endl;
    
    // Start loading via data loader
    m_dataLoader->loadRecording(dirPath.toStdString());
    updateStatus();
}

void PlayerWindow::autoLoadIfProvided(const QString &dirPath) {
    if (!dirPath.isEmpty()) {
        loadRecording(dirPath);
    }
}

void PlayerWindow::onLoadingStarted(const QString &path) {
    m_pathLabel->setText(tr("Loading: %1 ...").arg(path));
}

void PlayerWindow::onLoadingFinished(bool success, const QString &message) {
    m_pathLabel->setText(message);
    
    if (success) {
        const auto &data = m_dataLoader->getData();
        m_timelineSlider->setRange(0, static_cast<int>(data.totalFrames - 1));
        
        // Start prefetching from frame 0
        m_dataLoader->notifyFrameChanged(0);
        
        updateDisplays();
    } else {
        for (auto &p : m_panes) {
            p.content->setText("Load failed");
        }
    }
    updateStatus();
}

void PlayerWindow::onLoadingProgress(const QString &status) {
    m_pathLabel->setText(status);
}

void PlayerWindow::updateDisplays() {
    if (!m_dataLoader->isDataReady()) return;
    
    size_t idx = m_currentIndex;
    
    // Frame cameras
    for (int cam = 0; cam < 2; ++cam) {
        cv::Mat img = m_dataLoader->getFrameCameraFrame(cam, idx);
        if (!img.empty()) {
            QPixmap pm = QPixmap::fromImage(cvMatToQImage(img)).scaled(
                m_panes[cam].content->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_panes[cam].content->setPixmap(pm);
        } else {
            m_panes[cam].content->setText("(no frame)");
        }
    }
    
    // Event cameras
    for (int cam = 0; cam < 2; ++cam) {
        int paneIndex = 2 + cam; // bottom row
        QImage eventImg = m_dataLoader->getEventCameraFrame(cam, idx);
        if (!eventImg.isNull()) {
            QPixmap pm = QPixmap::fromImage(eventImg).scaled(
                m_panes[paneIndex].content->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
    size_t total = 1;
    if (m_dataLoader->isDataReady()) {
        total = m_dataLoader->getData().totalFrames;
    }
    
    size_t cur = std::min<size_t>(m_currentIndex.load(), total ? total - 1 : 0);
    double curTime = cur / m_assumedFps;
    double totTime = (total > 0 ? (total - 1) / m_assumedFps : 0.0);
    
    if (m_statusLabel) {
        m_statusLabel->setText(QString("Frame %1 / %2    %3 / %4")
            .arg(cur).arg(total ? total - 1 : 0)
            .arg(formatTime(curTime))
            .arg(formatTime(totTime)));
    }
}

void PlayerWindow::updateCachedFrames() {
    if (!m_dataLoader->isDataReady()) {
        return;
    }
    
    // Get all cached frame indices from the data loader
    QSet<int> cachedFrames = m_dataLoader->getAllCachedFrames();
    
    // Update the timeline slider with cached frame information
    m_timelineSlider->setCachedFrames(cachedFrames);
}

void PlayerWindow::updateFPS(size_t currentFrame) {
    auto now = std::chrono::steady_clock::now();
    
    // Calculate time difference since last frame change
    auto timeDiff = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastFrameTime);
    double deltaTime = timeDiff.count() / 1000000.0; // Convert to seconds
    
    // Calculate frame difference
    long long frameDiff = static_cast<long long>(currentFrame) - static_cast<long long>(m_lastFrameIndex);
    
    // Only calculate FPS for meaningful changes (avoid division by zero and very small time differences)
    if (deltaTime > 0.01 && abs(frameDiff) > 0) { // At least 10ms and at least 1 frame
        double instantFps = abs(frameDiff) / deltaTime;
        
        // Apply exponential smoothing to reduce jitter
        if (m_currentFps == 0.0) {
            m_currentFps = instantFps; // First measurement
        } else {
            m_currentFps = FPS_SMOOTHING * m_currentFps + (1.0 - FPS_SMOOTHING) * instantFps;
        }
        
        // Update FPS display
        if (m_fpsLabel) {
            m_fpsLabel->setText(QString("FPS: %1").arg(m_currentFps, 0, 'f', 1));
        }
    }
    
    // Update tracking variables
    m_lastFrameTime = now;
    m_lastFrameIndex = currentFrame;
}

void PlayerWindow::startRecording() {
    if (m_isRecording) {
        return; // Already recording
    }
    
    // Create default recording configuration
    RecordingManager::RecordingConfig config;
    // Use default settings for now - could be made configurable via GUI later
    config.eventFileFormat = "hdf5";
    config.recordingLengthSeconds = -1; // Indefinite recording
    
    // Debug output to compare with CLI
    std::cout << "GUI Recording Config:" << std::endl;
    std::cout << "  Event camera serials: " << (config.eventCameraSerials.empty() ? "auto-discovery" : "explicit") << std::endl;
    std::cout << "  Event file format: " << config.eventFileFormat << std::endl;
    std::cout << "  Biases provided: " << (config.biases.empty() ? "none (will use defaults)" : "yes") << std::endl;
    
    try {
        // If not configured yet, configure now
        if (!m_recordingManager->isConfigured()) {
            notifyStatus("Configuring cameras for first use...");
            if (!m_recordingManager->configure(config)) {
                QMessageBox::warning(this, tr("Recording Error"), 
                                   tr("Failed to configure cameras. Please check camera connections."));
                return;
            }
        }
        
        // Generate output directory for this recording
        std::string outputDir = generateRecordingDirectory();
        
        if (m_recordingManager->startRecording(outputDir)) {
            m_isRecording = true;
            m_recordButton->setText(tr("Stop Recording"));
            m_recordButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; font-weight: bold; }");
            m_recordingStatusLabel->setText(tr("Recording: 0.0s"));
            m_recordingTimer.start();
            
            // TODO: Switch to live mode display
            // This will be implemented in Phase 2 with RecordingBuffer integration
            
        } else {
            QMessageBox::warning(this, tr("Recording Error"), 
                               tr("Failed to start recording. Please check camera connections."));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Recording Error"), 
                            tr("Error starting recording: %1").arg(QString::fromStdString(e.what())));
    }
}

void PlayerWindow::stopRecording() {
    if (!m_isRecording) {
        return; // Not recording
    }
    
    try {
        QString recordingDir = QString::fromStdString(m_recordingManager->getCurrentOutputDirectory());
        m_recordingManager->stopRecording();
        
        // Explicitly close all camera devices to release file handles
        std::cout << "Closing camera devices to release file handles..." << std::endl;
        m_recordingManager->closeDevices();
        
        m_isRecording = false;
        m_recordButton->setText(tr("Start Recording"));
        m_recordButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
        m_recordingStatusLabel->setText("");
        m_recordingTimer.stop();
        
        // Auto-load the recorded folder for playback after a short delay
        // to ensure all files are properly written and closed
        if (!recordingDir.isEmpty() && QDir(recordingDir).exists()) {
            QMessageBox::information(this, tr("Recording Complete"), 
                                   tr("Recording saved to: %1\n\nLoading for playback...").arg(recordingDir));
            
            // Add a longer delay to ensure all files are properly closed, written, and synced
            // Event camera files may need more time to flush buffers and close file handles
            QTimer::singleShot(3000, this, [this, recordingDir]() {
                std::cout << "Auto-loading recorded folder after delay: " << recordingDir.toStdString() << std::endl;
                loadRecording(recordingDir);
            });
        }
        
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Recording Error"), 
                            tr("Error stopping recording: %1").arg(QString::fromStdString(e.what())));
        // Reset UI state anyway
        m_isRecording = false;
        m_recordButton->setText(tr("Start Recording"));
        m_recordButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
        m_recordingStatusLabel->setText("");
        m_recordingTimer.stop();
    }
}

void PlayerWindow::onRecordingToggle() {
    if (m_isRecording) {
        stopRecording();
    } else {
        startRecording();
    }
}

void PlayerWindow::onRecordingStatusUpdate(const QString &message) {
    // This slot receives status updates from the recording manager
    // For now, we'll just print to console, but could be used for more detailed status display
    std::cout << "Recording status: " << message.toStdString() << std::endl;
}

std::string PlayerWindow::generateRecordingDirectory() const {
    const auto now = std::chrono::system_clock::now();
    const auto time_t = std::chrono::system_clock::to_time_t(now);
    const auto tm = *std::localtime(&time_t);
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
    
    std::string outputDir = "./recording/" + std::string(timestamp);
    
    return outputDir;
}

void PlayerWindow::notifyStatus(const std::string& message) const {
    std::cout << "GUI Status: " << message << std::endl;
}
