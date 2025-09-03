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
#include "recording_data_loader.h"

#include <vector>
#include <atomic>

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
    
    // Data loader
    RecordingDataLoader *m_dataLoader {nullptr};
    
    std::atomic<size_t> m_currentIndex {0};
    double m_assumedFps {30.0};
};

// Utility functions
Pane createPane(const QString &title, const QColor &color);
