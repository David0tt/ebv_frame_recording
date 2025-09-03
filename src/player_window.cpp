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

#include <filesystem>
#include <algorithm>
#include <iostream>

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
    m_dataLoader = new RecordingDataLoader(this);
    connect(m_dataLoader, &RecordingDataLoader::loadingStarted, this, &PlayerWindow::onLoadingStarted);
    connect(m_dataLoader, &RecordingDataLoader::loadingFinished, this, &PlayerWindow::onLoadingFinished);
    connect(m_dataLoader, &RecordingDataLoader::loadingProgress, this, &PlayerWindow::onLoadingProgress);

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

    // Timer for updating cached frame display
    m_cacheUpdateTimer.setInterval(500); // Update every 500ms
    connect(&m_cacheUpdateTimer, &QTimer::timeout, this, &PlayerWindow::updateCachedFrames);
    m_cacheUpdateTimer.start();

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
        if (!m_dataLoader->isDataReady()) return;
        m_currentIndex = v;
        
        // Notify event camera loaders about frame change for prefetching
        m_dataLoader->notifyFrameChanged(v);
        
        updateDisplays();
        updateStatus();
    });
}

PlayerWindow::~PlayerWindow() {
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
    
    m_loadedDir = dirPath;
    m_currentIndex = 0;
    m_timelineSlider->setValue(0);
    m_timelineSlider->clearCachedFrames(); // Clear cache display for new recording

    // Clear panes
    for (auto &p : m_panes) {
        p.content->setText("Loading...");
    }

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
