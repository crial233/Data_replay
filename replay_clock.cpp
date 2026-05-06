#include "replay_clock.h"

#include <QtMath>

void ReplayClock::setBaseTimestampMs(qint64 timestampMs)
{
    m_baseTimestampMs = timestampMs;
}

void ReplayClock::setPlaybackRate(qreal rate)
{
    if (rate <= 0) {
        rate = 1.0;
    }

    const qint64 offset = currentOffsetMs();
    m_playbackRate = rate;

    if (m_playing) {
        m_pausedOffsetMs = offset;
        m_elapsed.restart();
    } else {
        m_pausedOffsetMs = offset;
    }
}

void ReplayClock::play()
{
    if (m_playing) {
        return;
    }

    m_playing = true;
    m_elapsed.restart();
}

void ReplayClock::pause()
{
    if (!m_playing) {
        return;
    }

    m_pausedOffsetMs = currentOffsetMs();
    m_playing = false;
}

void ReplayClock::seek(qint64 offsetMs)
{
    m_pausedOffsetMs = qMax<qint64>(0, offsetMs);
    if (m_playing) {
        m_elapsed.restart();
    }
}

void ReplayClock::reset()
{
    m_baseTimestampMs = 0;
    m_pausedOffsetMs = 0;
    m_playbackRate = 1.0;
    m_playing = false;
}

qint64 ReplayClock::currentOffsetMs() const
{
    if (!m_playing || !m_elapsed.isValid()) {
        return m_pausedOffsetMs;
    }

    return m_pausedOffsetMs + qRound64(m_elapsed.elapsed() * m_playbackRate);
}

qint64 ReplayClock::currentTimestampMs() const
{
    return m_baseTimestampMs + currentOffsetMs();
}
