#include "audio_player_manager.h"

#include "video_player_manager.h"

#include <QAudioOutput>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTime>
#include <QUrl>

#include <algorithm>

AudioPlayerManager::AudioPlayerManager(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_audioOutput);
}

void AudioPlayerManager::setAudioFolderPath(const QString &path)
{
    if (m_audioFolderPath == path) return;
    stop();
    m_audioFiles.clear();
    m_audioFolderPath = path;
}

void AudioPlayerManager::loadForDate(const QDate &date)
{
    stop();
    m_audioFiles.clear();
    if (m_audioFolderPath.isEmpty()) return;

    const qint64 dayStart = QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 dayEnd = dayStart + 24LL * 60 * 60 * 1000;
    QDir dir(m_audioFolderPath);
    const QStringList filters = {
        "*.mp3", "*.wav", "*.flac", "*.aac", "*.m4a", "*.ogg", "*.wma", "*.opus"
    };

    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &file : files) {
        bool ok = false;
        const qint64 timestamp = VideoPlayerManager::timestampFromVideoFileName(file.baseName(), &ok);
        if (ok && timestamp >= dayStart && timestamp < dayEnd) {
            m_audioFiles.append({file.absoluteFilePath(), timestamp});
        }
    }
    std::sort(m_audioFiles.begin(), m_audioFiles.end(), [](const AudioFile &a, const AudioFile &b) {
        return a.timestampMs < b.timestampMs;
    });
}

int AudioPlayerManager::fileIndexForTimestamp(qint64 timestampMs) const
{
    int result = -1;
    for (int i = 0; i < m_audioFiles.size(); ++i) {
        if (m_audioFiles[i].timestampMs > timestampMs) break;
        result = i;
    }
    return result;
}

void AudioPlayerManager::selectFile(int index)
{
    if (index == m_currentFileIndex) return;
    m_player->stop();
    m_currentFileIndex = index;
    if (index >= 0 && index < m_audioFiles.size()) {
        m_player->setSource(QUrl::fromLocalFile(m_audioFiles[index].path));
    } else {
        m_player->setSource(QUrl());
    }
}

void AudioPlayerManager::syncToTimestamp(qint64 timestampMs, qint64 thresholdMs, bool playing)
{
    int index = fileIndexForTimestamp(timestampMs);
    selectFile(index);
    if (index < 0) return;

    const qint64 desiredPosition = timestampMs - m_audioFiles[index].timestampMs;
    const qint64 duration = m_player->duration();
    if (duration > 0 && desiredPosition >= duration) {
        m_player->pause();
        return;
    }

    if (qAbs(m_player->position() - desiredPosition) > thresholdMs) {
        m_player->setPosition(desiredPosition);
    }
    if (playing) m_player->play();
    else m_player->pause();
}

void AudioPlayerManager::pause()
{
    m_player->pause();
}

void AudioPlayerManager::stop()
{
    m_player->stop();
    m_player->setSource(QUrl());
    m_currentFileIndex = -1;
}

void AudioPlayerManager::setPlaybackRate(qreal rate)
{
    m_player->setPlaybackRate(rate);
}
