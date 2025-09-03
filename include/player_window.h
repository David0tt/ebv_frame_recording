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

#include <vector>
#include <thread>
#include <atomic>

struct Pane {
    QFrame *frame {nullptr};
    QLabel *content {nullptr};
};

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

private:
    void loadDataWorker(const std::string &dirPath);
    void updateDisplays();
    void updateStatus();
    QString formatTime(double seconds) const;

    QPushButton *m_openButton {nullptr};
    QLabel *m_pathLabel {nullptr};
    QSlider *m_timelineSlider {nullptr};
    QPushButton *m_btnBack {nullptr};
    QPushButton *m_btnPlay {nullptr};
    QPushButton *m_btnFwd {nullptr};
    QTimer m_timer;
    QString m_loadedDir;
    std::vector<Pane> m_panes;
    QLabel *m_statusLabel {nullptr};
    std::thread m_loaderThread;
    std::atomic<bool> m_abortLoading {false};
    std::atomic<bool> m_dataReady {false};
    std::vector<FrameCameraData> m_frameCams;
    std::vector<EventCameraData> m_eventCams;
    std::atomic<size_t> m_currentIndex {0};
    size_t m_totalFrames {1};
    double m_assumedFps {30.0};
};

// Utility functions
Pane createPane(const QString &title, const QColor &color);
QImage cvMatToQImage(const cv::Mat &mat);
long long extract_frame_index(const std::string &pathStr);
