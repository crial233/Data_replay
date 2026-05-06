#ifndef REPLAY_TIMELINE_WIDGET_H
#define REPLAY_TIMELINE_WIDGET_H

#include <QPair>
#include <QVector>
#include <QWidget>

class ReplayTimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ReplayTimelineWidget(QWidget *parent = nullptr);

    void setPositionMs(qint64 positionMs);
    qint64 positionMs() const { return m_positionMs; }

    void setAvailabilityRanges(const QVector<QPair<qint64, qint64>> &ranges);

signals:
    void positionSelected(qint64 positionMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    static constexpr qint64 DAY_MS = 24LL * 60 * 60 * 1000;

    QRect trackRect() const;
    qint64 positionFromX(int x) const;
    int xFromPosition(qint64 positionMs) const;
    QString formatTimeOfDay(qint64 positionMs) const;

    QVector<QPair<qint64, qint64>> m_ranges;
    qint64 m_positionMs = 0;
    qint64 m_hoverPositionMs = -1;
};

#endif // REPLAY_TIMELINE_WIDGET_H
