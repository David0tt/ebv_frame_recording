#pragma once

#include <QWidget>
#include <QLabel>
#include <QFrame>
#include <QSlider>
#include <QPushButton>
#include <QTimer>
#include <QString>
#include <QImage>
#include <QResizeEvent>

#include <opencv2/opencv.hpp>
#include "recording_loader.h"
#include "cached_timeline_slider.h"

#include <vector>
#include <atomic>
#include <chrono>

struct Pane {
    QFrame *frame {nullptr};
    QLabel *content {nullptr};
};

class PlayerWindow : public QWidget {
    Q_OBJECT
public:
    explicit PlayerWindow(QWidget *parent = nullptr);
    ~PlayerWindow() override;

    void selectAndLoadFolder();
    void loadRecording(const QString &dirPath);
    void autoLoadIfProvided(const QString &dirPath);

protected:
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onLoadingStarted(const QString &path);
    void onLoadingFinished(bool success, const QString &message);
    void onLoadingProgress(const QString &status);

private:
    void updateDisplays();
    void updateStatus();
    void updateCachedFrames();
    void updateFPS(size_t currentFrame);
    QString formatTime(double seconds) const;

    QPushButton *m_openButton {nullptr};
    QLabel *m_pathLabel {nullptr};
    CachedTimelineSlider *m_timelineSlider {nullptr};
    QPushButton *m_btnBack {nullptr};
    QPushButton *m_btnPlay {nullptr};
    QPushButton *m_btnFwd {nullptr};
    QTimer m_timer;
    QTimer m_cacheUpdateTimer;
    QString m_loadedDir;
    std::vector<Pane> m_panes;
    QLabel *m_statusLabel {nullptr};
    QLabel *m_fpsLabel {nullptr};
    
    // Data loader
    RecordingLoader *m_dataLoader {nullptr};
    
    std::atomic<size_t> m_currentIndex {0};
    double m_assumedFps {30.0};
    
    // FPS tracking
    std::chrono::steady_clock::time_point m_lastFrameTime;
    size_t m_lastFrameIndex{0};
    double m_currentFps{0.0};
    static constexpr double FPS_SMOOTHING = 0.8; // Exponential smoothing factor
};

// Utility functions
Pane createPane(const QString &title, const QColor &color);
