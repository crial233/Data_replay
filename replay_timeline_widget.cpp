#include "replay_timeline_widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QToolTip>

#include <algorithm>

ReplayTimelineWidget::ReplayTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(62);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ReplayTimelineWidget::setPositionMs(qint64 positionMs)
{
    positionMs = std::clamp<qint64>(positionMs, 0, DAY_MS);
    if (m_positionMs == positionMs) {
        return;
    }

    m_positionMs = positionMs;
    update();
}

void ReplayTimelineWidget::setAvailabilityRanges(const QVector<QPair<qint64, qint64>> &ranges)
{
    m_ranges = ranges;
    update();
}

void ReplayTimelineWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QStyleOption option;
    option.initFrom(this);

    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect track = trackRect();
    const QRect labelBand(0, 2, width(), 18);
    painter.setPen(QPen(QColor(150, 148, 164), 1));
    for (int hour = 0; hour <= 24; ++hour) {
        const qint64 pos = (DAY_MS * hour) / 24;
        const int x = xFromPosition(pos);
        const int tickBottom = (hour % 2 == 0) ? 28 : 25;
        painter.drawLine(x, 20, x, tickBottom);

        if (hour % 2 == 0) {
            QRect textRect(x - 16, labelBand.top(), 32, labelBand.height());
            painter.drawText(textRect, Qt::AlignCenter, QString::number(hour));
        }
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(94, 94, 138));
    painter.drawRoundedRect(track, 2, 2);

    painter.setBrush(QColor(126, 132, 222));
    for (const auto &range : m_ranges) {
        const qint64 startMs = std::clamp<qint64>(range.first, 0, DAY_MS);
        const qint64 endMs = std::clamp<qint64>(range.second, startMs, DAY_MS);
        if (endMs <= startMs) {
            continue;
        }

        const int x1 = xFromPosition(startMs);
        const int x2 = xFromPosition(endMs);
        QRect availableRect(x1, track.top(), std::max(2, x2 - x1), track.height());
        painter.drawRoundedRect(availableRect, 2, 2);
    }

    if (m_hoverPositionMs >= 0) {
        const int hoverX = xFromPosition(m_hoverPositionMs);
        painter.setPen(QPen(QColor(230, 218, 160), 1));
        painter.drawLine(hoverX, track.top() - 8, hoverX, track.top() - 2);
    }

    const int playheadX = xFromPosition(m_positionMs);
    painter.setPen(QPen(QColor(226, 66, 94), 2));
    const int playheadTop = track.top() - 12;
    const int playheadBottom = track.top() + (track.height() * 3) / 5;
    painter.drawLine(playheadX, playheadTop, playheadX, playheadBottom);
    painter.setBrush(QColor(226, 66, 94));
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygon{
        QPoint(playheadX - 5, playheadTop),
        QPoint(playheadX + 5, playheadTop),
        QPoint(playheadX, playheadTop + 7)
    });
}

void ReplayTimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hoverPositionMs = positionFromX(event->position().toPoint().x());
    QToolTip::showText(event->globalPosition().toPoint(), formatTimeOfDay(m_hoverPositionMs), this);
    update();
}

void ReplayTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    const qint64 selectedMs = positionFromX(event->position().toPoint().x());
    setPositionMs(selectedMs);
    emit positionSelected(selectedMs);
}

void ReplayTimelineWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hoverPositionMs = -1;
    QToolTip::hideText();
    update();
}

QRect ReplayTimelineWidget::trackRect() const
{
    return QRect(16, 32, std::max(1, width() - 32), 20);
}

qint64 ReplayTimelineWidget::positionFromX(int x) const
{
    const QRect track = trackRect();
    const int clampedX = std::clamp(x, track.left(), track.right());
    const double ratio = static_cast<double>(clampedX - track.left()) / std::max(1, track.width());
    return std::clamp<qint64>(static_cast<qint64>(ratio * DAY_MS), 0, DAY_MS);
}

int ReplayTimelineWidget::xFromPosition(qint64 positionMs) const
{
    const QRect track = trackRect();
    positionMs = std::clamp<qint64>(positionMs, 0, DAY_MS);
    const double ratio = static_cast<double>(positionMs) / DAY_MS;
    return track.left() + static_cast<int>(ratio * track.width());
}

QString ReplayTimelineWidget::formatTimeOfDay(qint64 positionMs) const
{
    positionMs = std::clamp<qint64>(positionMs, 0, DAY_MS);
    const qint64 hours = positionMs / (60 * 60 * 1000);
    const qint64 minutes = (positionMs / (60 * 1000)) % 60;
    const qint64 seconds = (positionMs / 1000) % 60;
    const qint64 millis = positionMs % 1000;
    return QString("%1/%2/%3:%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}
