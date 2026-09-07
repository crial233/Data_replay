#include "video_player_manager.h"

#include <QAudioOutput>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QSet>
#include <QDateTime>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

VideoPlayerManager::VideoPlayerManager(QObject *parent)
    : QObject(parent)
{
    m_driftCorrectionTimer.start();
}

VideoPlayerManager::~VideoPlayerManager()
{
}

void VideoPlayerManager::createPlayers(QWidget *parentWidget)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i] = new QMediaPlayer(this);
        m_audioOutputs[i] = new QAudioOutput(this);
        m_players[i]->setAudioOutput(m_audioOutputs[i]);
        m_audioOutputs[i]->setVolume(0);  // 默认静音

        m_videoWidgets[i] = new QVideoWidget(parentWidget);
        m_videoWidgets[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_videoWidgets[i]->setAspectRatioMode(Qt::KeepAspectRatio);
        m_players[i]->setVideoOutput(m_videoWidgets[i]);

        // 通道容器: 叠加布局(视频 + 标签)
        m_channelContainers[i] = new QWidget(parentWidget);
        m_channelContainers[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_channelContainers[i]->setAutoFillBackground(true);
        m_channelContainers[i]->setStyleSheet("background: black;");
        auto *containerLayout = new QVBoxLayout(m_channelContainers[i]);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        containerLayout->addWidget(m_videoWidgets[i], 1);

        // 通道悬浮标签使用独立轻量窗口，避免播放时被 QVideoWidget 的原生渲染层覆盖。
        m_channelLabels[i] = new QLabel(QString("CH%1").arg(i), parentWidget,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
        m_channelLabels[i]->setStyleSheet(
            "QLabel { color: white; background: rgba(0,0,0,160); "
            "padding: 2px 8px; font: bold 12px; border-radius: 3px; }");
        m_channelLabels[i]->setAttribute(Qt::WA_ShowWithoutActivating);
        m_channelLabels[i]->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_channelLabels[i]->adjustSize();
        m_channelLabels[i]->move(4, 4);
        m_channelLabels[i]->raise();
        m_channelLabels[i]->hide();
        setChannelHint(i);
    }
    // 视频内嵌音轨保持静音，声音由单路音频播放器输出。
}

void VideoPlayerManager::setVideoGridWidget(QWidget *gridWidget)
{
    m_videoGridWidget = gridWidget;
    m_videoGridLayout = new QGridLayout(m_videoGridWidget);
    m_videoGridLayout->setContentsMargins(2, 2, 2, 2);
    m_videoGridLayout->setSpacing(2);
}

void VideoPlayerManager::setVideoListWidget(QListWidget *listWidget)
{
    m_videoListWidget = listWidget;
}

void VideoPlayerManager::setVideoFolderPath(const QString &path)
{
    m_videoFolderPath = path;
}

void VideoPlayerManager::setViewMode(ViewMode mode)
{
    m_viewMode = mode;
    updateVideoGrid();
    emit viewModeChanged(mode);
}

int VideoPlayerManager::channelForTimestamp(qint64 timestamp) const
{
    const quint8 channelMask = m_channelsByTimestamp.value(timestamp, 0);
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        if (channelMask & (quint8(1) << ch)) return ch;
    }
    return -1;
}

void VideoPlayerManager::showSingleChannel(int channel)
{
    if (channel < 0 || channel >= MAX_CHANNELS) {
        return;
    }

    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (m_channelLabels[i]) {
            m_channelLabels[i]->hide();
        }
    }

    m_activeChannel = channel;
    setViewMode(View1);
    setChannelOverlayVisible(channel, true);
}

void VideoPlayerManager::updateVideoGrid()
{
    m_forceDriftCorrection = true;
    // 隐藏所有视频容器
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_channelContainers[i]->hide();
    }
    
    // 彻底销毁旧布局，重新创建(避免残留的row/column stretch影响)
    QLayout *oldLayout = m_videoGridWidget->layout();
    if (oldLayout) {
        while (oldLayout->count() > 0) {
            oldLayout->takeAt(0);
        }
        delete oldLayout;
    }
    
    m_videoGridLayout = new QGridLayout(m_videoGridWidget);
    m_videoGridLayout->setContentsMargins(2, 2, 2, 2);
    m_videoGridLayout->setSpacing(2);
    
    switch (m_viewMode) {
    case View1:
        m_channelContainers[m_activeChannel]->show();
        m_videoGridLayout->addWidget(m_channelContainers[m_activeChannel], 0, 0);
        m_videoGridLayout->setRowStretch(0, 1);
        m_videoGridLayout->setColumnStretch(0, 1);
        break;
    case View4:
        for (int i = 0; i < 4; i++) {
            int channel = m_displayChannels[i];
            m_channelContainers[channel]->show();
            m_videoGridLayout->addWidget(m_channelContainers[channel], i / 2, i % 2);
        }
        m_videoGridLayout->setRowStretch(0, 1);
        m_videoGridLayout->setRowStretch(1, 1);
        m_videoGridLayout->setColumnStretch(0, 1);
        m_videoGridLayout->setColumnStretch(1, 1);
        break;
    case View8:
        for (int i = 0; i < MAX_CHANNELS; i++) {
            int channel = m_displayChannels[i];
            m_channelContainers[channel]->show();
            m_videoGridLayout->addWidget(m_channelContainers[channel], i / 4, i % 4);
        }
        m_videoGridLayout->setRowStretch(0, 1);
        m_videoGridLayout->setRowStretch(1, 1);
        m_videoGridLayout->setColumnStretch(0, 1);
        m_videoGridLayout->setColumnStretch(1, 1);
        m_videoGridLayout->setColumnStretch(2, 1);
        m_videoGridLayout->setColumnStretch(3, 1);
        break;
    }
}

void VideoPlayerManager::onVideoWidgetClicked(int channel)
{
    if (m_viewMode == View1 && m_activeChannel == channel) {
        setViewMode(View4);
    } else {
        m_activeChannel = channel;
        setViewMode(View1);
    }
}

int VideoPlayerManager::displaySlotForChannel(int channel) const
{
    int slotCount = (m_viewMode == View8) ? MAX_CHANNELS : ((m_viewMode == View4) ? 4 : 1);
    if (m_viewMode == View1) {
        return (channel == m_activeChannel) ? 0 : -1;
    }

    for (int slot = 0; slot < slotCount; ++slot) {
        if (m_displayChannels[slot] == channel) {
            return slot;
        }
    }
    return -1;
}

void VideoPlayerManager::setDisplaySlotChannel(int slot, int channel)
{
    if (slot < 0 || slot >= MAX_CHANNELS || channel < 0 || channel >= MAX_CHANNELS) {
        return;
    }

    int existingSlot = -1;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (m_displayChannels[i] == channel) {
            existingSlot = i;
            break;
        }
    }

    if (existingSlot >= 0 && existingSlot != slot) {
        std::swap(m_displayChannels[slot], m_displayChannels[existingSlot]);
    } else {
        m_displayChannels[slot] = channel;
    }

    updateVideoGrid();
}

void VideoPlayerManager::setChannelVideoVisible(int channel, bool visible)
{
    if (channel < 0 || channel >= MAX_CHANNELS || !m_videoWidgets[channel]) {
        return;
    }

    if (m_videoWidgets[channel]->isVisible() != visible) {
        m_videoWidgets[channel]->setVisible(visible);
    }
}

bool VideoPlayerManager::isChannelDisplayed(int channel) const
{
    if (m_viewMode == View1) return channel == m_activeChannel;
    const int slotCount = (m_viewMode == View8) ? MAX_CHANNELS : 4;
    for (int slot = 0; slot < slotCount; ++slot) {
        if (m_displayChannels[slot] == channel) return true;
    }
    return false;
}

void VideoPlayerManager::setChannelHint(int channel, const QString &detail)
{
    if (channel < 0 || channel >= MAX_CHANNELS) {
        return;
    }

    QString hint = QString("CH%1").arg(channel);
    if (!detail.isEmpty()) {
        hint += QString(" - %1").arg(detail);
    }

    if (m_channelContainers[channel]) {
        m_channelContainers[channel]->setToolTip(hint);
    }
    if (m_videoWidgets[channel]) {
        m_videoWidgets[channel]->setToolTip(hint);
    }
    if (m_channelLabels[channel]) {
        m_channelLabels[channel]->setToolTip(hint);
    }
}

void VideoPlayerManager::setChannelOverlayVisible(int channel, bool visible)
{
    if (channel < 0 || channel >= MAX_CHANNELS || !m_channelLabels[channel]) {
        return;
    }

    m_channelLabels[channel]->setText(QString("CH%1").arg(channel));
    m_channelLabels[channel]->adjustSize();
    if (m_channelContainers[channel]) {
        m_channelLabels[channel]->move(m_channelContainers[channel]->mapToGlobal(QPoint(4, 4)));
    }
    m_channelLabels[channel]->raise();
    m_channelLabels[channel]->setVisible(visible);
}

void VideoPlayerManager::syncAllPlayersToMaster()
{
    qint64 masterPos = m_players[0]->position();
    for (int i = 1; i < MAX_CHANNELS; i++) {
        m_players[i]->setPosition(masterPos);
    }
}

void VideoPlayerManager::togglePlayback()
{
    bool isPlaying = (m_players[0]->playbackState() == QMediaPlayer::PlayingState);
    
    if (isPlaying) {
        pauseAll();
    } else {
        playAll();
    }
}

void VideoPlayerManager::playAll()
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channelStartTimestamps[i] > 0 && isChannelDisplayed(i) &&
            m_players[i]->playbackState() != QMediaPlayer::PlayingState) {
            m_players[i]->play();
        }
    }
}

void VideoPlayerManager::pauseAll()
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_players[i]->playbackState() != QMediaPlayer::PausedState) {
            m_players[i]->pause();
        }
    }
}

void VideoPlayerManager::setPlaybackRate(qreal rate)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->setPlaybackRate(rate);
    }
}

void VideoPlayerManager::seekAllPlayers(qint64 positionMs)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->setPosition(positionMs);
    }
}

void VideoPlayerManager::syncPlayersToPosition(qint64 positionMs, qint64 thresholdMs)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channelVideos[i].isEmpty()) {
            continue;
        }

        if (qAbs(m_players[i]->position() - positionMs) > thresholdMs) {
            m_players[i]->setPosition(positionMs);
        }
    }
}

void VideoPlayerManager::syncPlayersToTimestamp(qint64 currentTimestampMs, qint64 thresholdMs, bool playing)
{
    // Seeking several HEVC streams is expensive. During normal playback, correct
    // drift at most once per second instead of on every 50 ms UI clock tick.
    const bool forceSeek = thresholdMs <= 0 || !playing;
    const bool correctionDue = forceSeek || m_forceDriftCorrection ||
                               m_driftCorrectionTimer.elapsed() >= 1000;
    const qint64 effectiveThresholdMs = forceSeek ? thresholdMs : qMax<qint64>(500, thresholdMs);
    if (correctionDue) {
        m_driftCorrectionTimer.restart();
        m_forceDriftCorrection = false;
    }

    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (isChannelDisplayed(i)) {
            selectVideoForTimestamp(i, currentTimestampMs);
        }
        if (m_channelStartTimestamps[i] <= 0) {
            setChannelVideoVisible(i, false);
            continue;
        }

        if (!isChannelDisplayed(i)) {
            if (m_players[i]->playbackState() == QMediaPlayer::PlayingState) {
                m_players[i]->pause();
            }
            continue;
        }

        const qint64 desiredPositionMs = currentTimestampMs - m_channelStartTimestamps[i];
        if (desiredPositionMs < 0) {
            if (correctionDue && m_players[i]->position() != 0) {
                m_players[i]->setPosition(0);
            }
            if (m_players[i]->playbackState() != QMediaPlayer::PausedState) m_players[i]->pause();
            setChannelVideoVisible(i, false);
            continue;
        }

        const qint64 durationMs = m_players[i]->duration();
        if (durationMs > 0 && desiredPositionMs > durationMs) {
            if (correctionDue && qAbs(m_players[i]->position() - durationMs) > effectiveThresholdMs) {
                m_players[i]->setPosition(durationMs);
            }
            if (m_players[i]->playbackState() != QMediaPlayer::PausedState) m_players[i]->pause();
            setChannelVideoVisible(i, false);
            continue;
        }

        setChannelVideoVisible(i, true);
        if (correctionDue && qAbs(m_players[i]->position() - desiredPositionMs) > effectiveThresholdMs) {
            m_players[i]->setPosition(desiredPositionMs);
        }

        if (playing) {
            if (m_players[i]->playbackState() != QMediaPlayer::PlayingState) m_players[i]->play();
        } else {
            if (m_players[i]->playbackState() != QMediaPlayer::PausedState) m_players[i]->pause();
        }
    }
}

bool VideoPlayerManager::selectVideoForTimestamp(int channel, qint64 timestamp)
{
    const auto &videos = m_channelVideos[channel];
    const auto upper = std::upper_bound(videos.cbegin(), videos.cend(), timestamp,
                                        [](qint64 ts, const VideoEntry &video) {
        return ts < video.timestamp;
    });
    const int index = (upper == videos.cbegin())
        ? -1
        : static_cast<int>(std::distance(videos.cbegin(), upper) - 1);

    if (index == m_currentVideoIndices[channel]) return index >= 0;

    m_currentVideoIndices[channel] = index;
    if (index < 0) {
        m_channelStartTimestamps[channel] = 0;
        m_players[channel]->stop();
        setChannelVideoVisible(channel, false);
        setChannelHint(channel, tr("等待视频"));
        return false;
    }

    const VideoEntry &video = videos[index];
    m_channelStartTimestamps[channel] = video.timestamp;
    m_players[channel]->setSource(QUrl::fromLocalFile(video.path));
    m_players[channel]->setPosition(qMax<qint64>(0, timestamp - video.timestamp));
    setChannelHint(channel, QFileInfo(video.path).fileName());
    m_forceDriftCorrection = true;
    return true;
}

qint64 VideoPlayerManager::sessionDurationMs(qint64 baseTimestampMs) const
{
    qint64 durationMs = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (m_channelStartTimestamps[i] <= 0) {
            continue;
        }

        const qint64 startOffsetMs = m_channelStartTimestamps[i] - baseTimestampMs;
        const qint64 channelDurationMs = qMax<qint64>(0, m_players[i]->duration());
        durationMs = qMax(durationMs, startOffsetMs + channelDurationMs);
    }
    return qMax<qint64>(0, durationMs);
}

QVector<QPair<qint64, qint64>> VideoPlayerManager::availabilityRangesForDate(const QDate &date) const
{
    static constexpr qint64 DEFAULT_MARK_MS = 3000;
    static constexpr qint64 DAY_MS = 24LL * 60 * 60 * 1000;

    const qint64 dayStart = timestampFromDate(date);
    QVector<QPair<qint64, qint64>> ranges;
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        for (const auto &video : m_channelVideos[ch]) {
            const qint64 start = video.timestamp - dayStart;
            if (start < 0 || start >= DAY_MS) {
                continue;
            }
            ranges.append(qMakePair(start, qMin(start + DEFAULT_MARK_MS, DAY_MS)));
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });

    QVector<QPair<qint64, qint64>> merged;
    for (const auto &range : ranges) {
        if (merged.isEmpty() || range.first > merged.last().second) {
            merged.append(range);
        } else {
            merged.last().second = qMax(merged.last().second, range.second);
        }
    }
    return merged;
}

void VideoPlayerManager::stopAll()
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->stop();
        setChannelVideoVisible(i, false);
    }
}

qint64 VideoPlayerManager::timestampFromDate(const QDate &date)
{
    QDateTime dateTime(date, QTime(0, 0, 0));
    return dateTime.toMSecsSinceEpoch();
}

qint64 VideoPlayerManager::timestampFromVideoFileName(const QString &baseName, bool *ok)
{
    bool parsed = false;
    qint64 timestamp = baseName.toLongLong(&parsed);
    if (!parsed) {
        const QDateTime dateTime = QDateTime::fromString(baseName, "yyyy-MM-dd_HH-mm-ss-zzz");
        parsed = dateTime.isValid();
        if (parsed) {
            timestamp = dateTime.toMSecsSinceEpoch();
        }
    }

    if (ok) {
        *ok = parsed;
    }
    return parsed ? timestamp : 0;
}

void VideoPlayerManager::loadAllChannels(const QDate &date)
{
    const quint64 generation = ++m_scanGeneration;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->stop();
        setChannelVideoVisible(i, false);
        m_channelVideos[i].clear();
        m_channelStartTimestamps[i] = 0;
        m_currentVideoIndices[i] = -1;
    }
    m_channelsByTimestamp.clear();
    emit canCacheClearRequested();

    if (m_videoListWidget) m_videoListWidget->clear();

    if (m_videoFolderPath.isEmpty()) {
        emit videoListLoaded(false);
        return;
    }

    const QString folderPath = m_videoFolderPath;
    emit statusMessage(tr("正在扫描 %1 的视频...").arg(date.toString("yyyy-MM-dd")));
    auto *watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this,
            [this, watcher, date, generation]() {
        const ScanResult result = watcher->result();
        watcher->deleteLater();
        applyScanResult(result, date, generation);
    });
    watcher->setFuture(QtConcurrent::run([folderPath, date]() {
        return scanVideoFolder(folderPath, date);
    }));
}

VideoPlayerManager::ScanResult VideoPlayerManager::scanVideoFolder(
    const QString &folderPath, const QDate &date)
{
    ScanResult result;

    qint64 dayStart = timestampFromDate(date);
    qint64 dayEnd = dayStart + 24 * 60 * 60 * 1000;
    QStringList filters;
    filters << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv";
    
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        QString channelDir = QString("%1/video%2").arg(folderPath).arg(ch);
        QDir dir(channelDir);
        
        if (!dir.exists()) continue;
        
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);
        bool chHasVideo = false;
        
        for (const auto &file : fileList) {
            QString baseName = file.baseName();
            bool ok = false;
            qint64 timestamp = timestampFromVideoFileName(baseName, &ok);
            
            if (ok && timestamp >= dayStart && timestamp < dayEnd) {
                result.channels[ch].append({timestamp, file.absoluteFilePath(), ch});
                result.channelsByTimestamp[timestamp] |= (quint8(1) << ch);
                result.totalVideos++;
                chHasVideo = true;
            }
        }
        std::sort(result.channels[ch].begin(), result.channels[ch].end(),
                  [](const VideoEntry &a, const VideoEntry &b) {
            return a.timestamp < b.timestamp;
        });
        if (chHasVideo) result.activeChannels++;
    }
    return result;
}

void VideoPlayerManager::applyScanResult(
    const ScanResult &result, const QDate &date, quint64 generation)
{
    if (generation != m_scanGeneration) return;

    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        m_channelVideos[ch] = result.channels[ch];
    }
    m_channelsByTimestamp = result.channelsByTimestamp;

    QList<qint64> sortedTimestamps = m_channelsByTimestamp.keys();
    std::sort(sortedTimestamps.begin(), sortedTimestamps.end());

    if (m_videoListWidget) {
        const QSignalBlocker blocker(m_videoListWidget);
        m_videoListWidget->setUpdatesEnabled(false);
        for (qint64 ts : sortedTimestamps) {
            QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(ts);
            QStringList channels;
            const quint8 channelMask = m_channelsByTimestamp.value(ts);
            for (int ch = 0; ch < MAX_CHANNELS; ch++) {
                if (channelMask & (quint8(1) << ch)) channels << QString("CH%1").arg(ch);
            }
            QString displayText = QString("%1  %2")
                .arg(dateTime.toString("HH:mm:ss"))
                .arg(channels.join(","));
            auto *item = new QListWidgetItem(displayText, m_videoListWidget);
            item->setData(Qt::UserRole, ts);
        }
        m_videoListWidget->setUpdatesEnabled(true);
    }

    if (result.totalVideos > 0) {
        emit statusMessage(tr("找到 %1 个视频文件(跨 %2 通道)")
                           .arg(result.totalVideos).arg(result.activeChannels));
    } else {
        if (m_videoListWidget) {
            m_videoListWidget->addItem(tr("该日期没有视频文件"));
        }
        emit statusMessage(tr("%1 没有找到视频文件").arg(date.toString("yyyy-MM-dd")));
    }
    emit videoListLoaded(result.totalVideos > 0);
}

void VideoPlayerManager::playAllChannels(qint64 timestamp)
{
    emit canCacheClearRequested();
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) m_currentVideoIndices[ch] = -1;
    syncPlayersToTimestamp(timestamp, 0, true);
    emit canLogLoadRequested(timestamp);
}

bool VideoPlayerManager::handleEventFilter(QObject *watched, QEvent *event, QWidget *menuParent)
{
    // 查找是哪个通道的容器或视频控件
    int channel = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (watched == m_channelContainers[i] || watched == m_videoWidgets[i]) {
            channel = i;
            break;
        }
    }
    if (channel < 0) {
        return false;
    }

    if (event->type() == QEvent::Enter) {
        setChannelOverlayVisible(channel, true);
        return false;
    }

    if (event->type() == QEvent::Leave) {
        QWidget *container = m_channelContainers[channel];
        if (!container || !container->underMouse()) {
            setChannelOverlayVisible(channel, false);
        }
        return false;
    }

    if (event->type() == QEvent::ToolTip) {
        return true;
    }
    
    // 双击: 切换1路/4路视图
    if (event->type() == QEvent::MouseButtonDblClick) {
        onVideoWidgetClicked(channel);
        return true;
    }
    
    // 右键: 弹出通道切换菜单
    if (event->type() == QEvent::ContextMenu) {
        auto *mouseEvent = static_cast<QContextMenuEvent*>(event);
        QMenu menu(menuParent);
        int slot = displaySlotForChannel(channel);
        
        auto *switchMenu = menu.addMenu(tr("当前窗口显示通道"));
        for (int i = 0; i < MAX_CHANNELS; i++) {
            QString label = QString("CH%1").arg(i);
            label += !m_channelVideos[i].isEmpty() ? tr(" - 有视频") : tr(" - 无视频");
            QAction *action = switchMenu->addAction(label);
            action->setData(i);
            action->setCheckable(true);
            action->setChecked(i == channel);
        }
        
        menu.addSeparator();
        QAction *act1 = menu.addAction(tr("1路视图"));
        QAction *act4 = menu.addAction(tr("4路视图"));
        QAction *act8 = menu.addAction(tr("8路视图"));
        act1->setCheckable(true); act1->setChecked(m_viewMode == View1);
        act4->setCheckable(true); act4->setChecked(m_viewMode == View4);
        act8->setCheckable(true); act8->setChecked(m_viewMode == View8);
        
        QAction *selected = menu.exec(mouseEvent->globalPos());
        if (!selected) return true;
        
        if (qobject_cast<QMenu*>(selected->parent()) == switchMenu) {
            int ch = selected->data().toInt();
            if (m_viewMode == View1) {
                m_activeChannel = ch;
                updateVideoGrid();
            } else {
                setDisplaySlotChannel(slot, ch);
            }
        } else if (selected == act1) {
            m_activeChannel = channel;
            setViewMode(View1);
        } else if (selected == act4) {
            setViewMode(View4);
        } else if (selected == act8) {
            setViewMode(View8);
        }
        return true;
    }
    
    return false;
}
