#ifndef VIDEO_PLAYER_MANAGER_H
#define VIDEO_PLAYER_MANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QDate>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QAudioOutput>
#include <QGridLayout>
#include <QHash>
#include <QElapsedTimer>

class QLabel;
class QListWidget;

class VideoPlayerManager : public QObject
{
    Q_OBJECT

public:
    enum ViewMode { View1 = 1, View4 = 4, View8 = 8 };
    Q_ENUM(ViewMode)

    static constexpr int MAX_CHANNELS = 8;

    explicit VideoPlayerManager(QObject *parent = nullptr);
    ~VideoPlayerManager();

    // 创建8路播放器和视频widget（在buildUi中调用）
    void createPlayers(QWidget *parentWidget);

    // 设置视频网格容器
    void setVideoGridWidget(QWidget *gridWidget);

    // 设置视频列表widget（由MainWindow注入）
    void setVideoListWidget(QListWidget *listWidget);

    // 视图模式
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }
    int activeChannel() const { return m_activeChannel; }
    void setActiveChannel(int ch) { m_activeChannel = ch; }
    int channelForTimestamp(qint64 timestamp) const;
    void showSingleChannel(int channel);

    // 视频文件夹路径
    void setVideoFolderPath(const QString &path);
    QString videoFolderPath() const { return m_videoFolderPath; }

    // 加载所有通道的视频
    void loadAllChannels(const QDate &date);

    // 按时间戳播放所有通道
    void playAllChannels(qint64 timestamp);

    // 同步所有播放器到主播放器
    void syncAllPlayersToMaster();

    // 播放控制
    void togglePlayback();
    void playAll();
    void pauseAll();
    void setPlaybackRate(qreal rate);
    void seekAllPlayers(qint64 positionMs);
    void syncPlayersToPosition(qint64 positionMs, qint64 thresholdMs);
    void syncPlayersToTimestamp(qint64 currentTimestampMs, qint64 thresholdMs, bool playing);
    qint64 sessionDurationMs(qint64 baseTimestampMs) const;
    QVector<QPair<qint64, qint64>> availabilityRangesForDate(const QDate &date) const;

    // 更新视频网格布局
    void updateVideoGrid();

    // 点击视频widget处理
    void onVideoWidgetClicked(int channel);

    // 获取主播放器（通道0）
    QMediaPlayer *masterPlayer() const { return m_players[0]; }

    // 获取指定通道播放器
    QMediaPlayer *player(int channel) const { return m_players[channel]; }

    // 获取通道容器（用于eventFilter）
    QWidget *channelContainer(int channel) const { return m_channelContainers[channel]; }
    QWidget *videoWidget(int channel) const { return m_videoWidgets[channel]; }

    // 获取视频文件列表（判断是否有视频）
    bool hasVideo(int channel) const { return !m_channelVideos[channel].isEmpty(); }

    // 停止所有播放器
    void stopAll();

    // 事件过滤
    bool handleEventFilter(QObject *watched, QEvent *event, QWidget *menuParent);

    // 时间戳解析工具（公有，供 CanDataManager 调用）
    static qint64 timestampFromVideoFileName(const QString &baseName, bool *ok = nullptr);

    // 以下函数原先为 public，保持此访问级别
    int displaySlotForChannel(int channel) const;
    void setDisplaySlotChannel(int slot, int channel);
    void setChannelVideoVisible(int channel, bool visible);
    void setChannelHint(int channel, const QString &detail = QString());
    void setChannelOverlayVisible(int channel, bool visible);

signals:
    void viewModeChanged(ViewMode mode);
    void statusMessage(const QString &message);
    void canCacheClearRequested();
    void canLogLoadRequested(qint64 timestamp);
    void videoListLoaded(bool hasVideos);

private:
    struct VideoEntry {
        qint64 timestamp = 0;
        QString path;
        int channel = -1;
    };

    struct ScanResult {
        QVector<VideoEntry> channels[MAX_CHANNELS];
        QHash<qint64, quint8> channelsByTimestamp;
        int totalVideos = 0;
        int activeChannels = 0;
    };

    static ScanResult scanVideoFolder(const QString &folderPath, const QDate &date);
    void applyScanResult(const ScanResult &result, const QDate &date, quint64 generation);
    bool isChannelDisplayed(int channel) const;
    bool selectVideoForTimestamp(int channel, qint64 timestamp);

    // 8路播放器
    QMediaPlayer *m_players[MAX_CHANNELS] = {};
    QAudioOutput *m_audioOutputs[MAX_CHANNELS] = {};
    QVideoWidget *m_videoWidgets[MAX_CHANNELS] = {};
    QLabel *m_channelLabels[MAX_CHANNELS] = {};
    QWidget *m_channelContainers[MAX_CHANNELS] = {};

    // 视频网格
    QGridLayout *m_videoGridLayout = nullptr;
    QWidget *m_videoGridWidget = nullptr;

    // 视频列表
    QListWidget *m_videoListWidget = nullptr;

    // 视图模式
    ViewMode m_viewMode = View4;
    int m_activeChannel = 0;

    // 视频文件夹
    QString m_videoFolderPath;
    QVector<VideoEntry> m_channelVideos[MAX_CHANNELS];
    QHash<qint64, quint8> m_channelsByTimestamp;
    qint64 m_channelStartTimestamps[MAX_CHANNELS] = {};
    int m_currentVideoIndices[MAX_CHANNELS] = {-1, -1, -1, -1, -1, -1, -1, -1};
    quint64 m_scanGeneration = 0;
    QElapsedTimer m_driftCorrectionTimer;
    bool m_forceDriftCorrection = true;

    // 每个显示格子对应的视频通道
    int m_displayChannels[MAX_CHANNELS] = {0, 1, 2, 3, 4, 5, 6, 7};

    // 日期转时间戳工具（私有）
    static qint64 timestampFromDate(const QDate &date);
};

#endif // VIDEO_PLAYER_MANAGER_H
