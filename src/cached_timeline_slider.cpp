#include "cached_timeline_slider.h"
#include <QStyleOptionSlider>
#include <QStyle>
#include <QPaintEvent>
#include <QMutexLocker>
#include <algorithm>

CachedTimelineSlider::CachedTimelineSlider(Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent) {
    // Set default properties
    setMinimumHeight(20);
}

void CachedTimelineSlider::setCachedFrames(const QSet<int> &cachedFrames) {
    QMutexLocker locker(&m_cacheMutex);
    m_cachedFrames = cachedFrames;
    update(); // Trigger repaint
}

void CachedTimelineSlider::addCachedFrame(int frameIndex) {
    QMutexLocker locker(&m_cacheMutex);
    m_cachedFrames.insert(frameIndex);
    update(); // Trigger repaint
}

void CachedTimelineSlider::clearCachedFrames() {
    QMutexLocker locker(&m_cacheMutex);
    m_cachedFrames.clear();
    update(); // Trigger repaint
}

void CachedTimelineSlider::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Get style option for the slider
    QStyleOptionSlider option;
    initStyleOption(&option);

    // Get the groove rectangle (the track area)
    QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    
    // Draw the background groove first
    painter.fillRect(grooveRect, m_backgroundColor);
    
    // Draw cached ranges
    drawCachedRanges(painter, grooveRect);
    
    // Now draw the standard slider components on top
    QSlider::paintEvent(event);
}

void CachedTimelineSlider::drawCachedRanges(QPainter &painter, const QRect &grooveRect) {
    QMutexLocker locker(&m_cacheMutex);
    
    if (m_cachedFrames.isEmpty() || maximum() <= minimum()) {
        return;
    }
    
    // Convert cached frame indices to sorted list for range detection
    QList<int> sortedFrames;
    for (const auto &frame : m_cachedFrames) {
        sortedFrames.append(frame);
    }
    std::sort(sortedFrames.begin(), sortedFrames.end());
    
    // Group consecutive frames into ranges
    QList<QPair<int, int>> cachedRanges;
    
    if (!sortedFrames.isEmpty()) {
        int rangeStart = sortedFrames.first();
        int rangeEnd = rangeStart;
        
        for (int i = 1; i < sortedFrames.size(); ++i) {
            if (sortedFrames[i] == rangeEnd + 1) {
                // Consecutive frame, extend current range
                rangeEnd = sortedFrames[i];
            } else {
                // Gap found, save current range and start new one
                cachedRanges.append(qMakePair(rangeStart, rangeEnd));
                rangeStart = sortedFrames[i];
                rangeEnd = rangeStart;
            }
        }
        // Don't forget the last range
        cachedRanges.append(qMakePair(rangeStart, rangeEnd));
    }
    
    // Draw each cached range
    painter.setBrush(m_cachedColor);
    painter.setPen(Qt::NoPen);
    
    const int sliderRange = maximum() - minimum();
    const int grooveWidth = grooveRect.width();
    
    for (const auto &range : cachedRanges) {
        // Calculate positions as percentage of the total range
        double startPercent = static_cast<double>(range.first - minimum()) / sliderRange;
        double endPercent = static_cast<double>(range.second + 1 - minimum()) / sliderRange; // +1 to include the end frame
        
        // Clamp to valid range
        startPercent = qBound(0.0, startPercent, 1.0);
        endPercent = qBound(0.0, endPercent, 1.0);
        
        // Convert to pixel positions
        int startX = grooveRect.left() + static_cast<int>(startPercent * grooveWidth);
        int endX = grooveRect.left() + static_cast<int>(endPercent * grooveWidth);
        
        // Ensure minimum width for visibility
        int width = qMax(2, endX - startX);
        
        QRect cacheRect(startX, grooveRect.top(), width, grooveRect.height());
        painter.fillRect(cacheRect, m_cachedColor);
    }
}
