#ifndef AUDIO_PLAYER_MANAGER_H
#define AUDIO_PLAYER_MANAGER_H

#include <QObject>
#include <QDate>
#include <QString>
#include <QVector>

class QAudioOutput;
class QMediaPlayer;

class AudioPlayerManager : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayerManager(QObject *parent = nullptr);

    void setAudioFolderPath(const QString &path);
    QString audioFolderPath() const { return m_audioFolderPath; }
    void loadForDate(const QDate &date);
    bool hasAudio() const { return !m_audioFiles.isEmpty(); }
    int audioFileCount() const { return m_audioFiles.size(); }

    void syncToTimestamp(qint64 timestampMs, qint64 thresholdMs, bool playing);
    void pause();
    void stop();
    void setPlaybackRate(qreal rate);

private:
    struct AudioFile {
        QString path;
        qint64 timestampMs = 0;
    };

    int fileIndexForTimestamp(qint64 timestampMs) const;
    void selectFile(int index);

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QString m_audioFolderPath;
    QVector<AudioFile> m_audioFiles;
    int m_currentFileIndex = -1;
};

#endif
