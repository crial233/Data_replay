#ifndef REPLAY_CLOCK_H
#define REPLAY_CLOCK_H

#include <QElapsedTimer>
#include <QtGlobal>

class ReplayClock
{
public:
    void setBaseTimestampMs(qint64 timestampMs);
    qint64 baseTimestampMs() const { return m_baseTimestampMs; }

    void setPlaybackRate(qreal rate);
    qreal playbackRate() const { return m_playbackRate; }

    void play();
    void pause();
    void seek(qint64 offsetMs);
    void reset();

    bool isPlaying() const { return m_playing; }
    qint64 currentOffsetMs() const;
    qint64 currentTimestampMs() const;

private:
    qint64 m_baseTimestampMs = 0;
    qint64 m_pausedOffsetMs = 0;
    qreal m_playbackRate = 1.0;
    bool m_playing = false;
    QElapsedTimer m_elapsed;
};

#endif // REPLAY_CLOCK_H
