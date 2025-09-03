#pragma once

#include <QSlider>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QSet>
#include <QMutex>

class CachedTimelineSlider : public QSlider {
    Q_OBJECT

public:
    explicit CachedTimelineSlider(Qt::Orientation orientation, QWidget *parent = nullptr);
    
    // Methods to manage cached frame ranges
    void setCachedFrames(const QSet<int> &cachedFrames);
    void addCachedFrame(int frameIndex);
    void clearCachedFrames();
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    void drawCachedRanges(QPainter &painter, const QRect &grooveRect);
    QSet<int> m_cachedFrames;
    QMutex m_cacheMutex;
    
    // Visual properties
    QColor m_cachedColor{200, 200, 200, 120}; // Light gray with transparency
    QColor m_backgroundColor{100, 100, 100}; // Darker gray for uncached areas
};
