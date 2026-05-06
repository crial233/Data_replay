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

#include <algorithm>

VideoPlayerManager::VideoPlayerManager(QObject *parent)
    : QObject(parent)
{
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
        m_videoWidgets[i]->setAspectRatioMode(Qt::IgnoreAspectRatio);
        m_players[i]->setVideoOutput(m_videoWidgets[i]);

        // 通道容器: 叠加布局(视频 + 标签)
        m_channelContainers[i] = new QWidget(parentWidget);
        m_channelContainers[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *containerLayout = new QVBoxLayout(m_channelContainers[i]);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);
        containerLayout->addWidget(m_videoWidgets[i], 1);

        // 通道标签(覆盖在视频上方)
        m_channelLabels[i] = new QLabel(QString("CH%1").arg(i), m_channelContainers[i]);
        m_channelLabels[i]->setStyleSheet(
            "QLabel { color: white; background: rgba(0,0,0,160); "
            "padding: 2px 8px; font: bold 12px; border-radius: 3px; }");
        m_channelLabels[i]->adjustSize();
        m_channelLabels[i]->move(4, 4);
        m_channelLabels[i]->raise();
    }
    // 通道0有声音
    m_audioOutputs[0]->setVolume(50);
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

void VideoPlayerManager::updateVideoGrid()
{
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
            m_channelContainers[i]->show();
            m_videoGridLayout->addWidget(m_channelContainers[i], i / 2, i % 2);
        }
        m_videoGridLayout->setRowStretch(0, 1);
        m_videoGridLayout->setRowStretch(1, 1);
        m_videoGridLayout->setColumnStretch(0, 1);
        m_videoGridLayout->setColumnStretch(1, 1);
        break;
    case View8:
        for (int i = 0; i < MAX_CHANNELS; i++) {
            m_channelContainers[i]->show();
            m_videoGridLayout->addWidget(m_channelContainers[i], i / 4, i % 4);
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
        for (int i = 0; i < MAX_CHANNELS; i++) {
            m_players[i]->pause();
        }
    } else {
        for (int i = 0; i < MAX_CHANNELS; i++) {
            if (m_players[i]->playbackState() == QMediaPlayer::StoppedState) {
                // stopped时不重置CAN缓存，让调用者处理
            }
            m_players[i]->play();
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

void VideoPlayerManager::stopAll()
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->stop();
    }
}

qint64 VideoPlayerManager::timestampFromDate(const QDate &date)
{
    QDateTime dateTime(date, QTime(0, 0, 0));
    return dateTime.toMSecsSinceEpoch();
}

void VideoPlayerManager::loadAllChannels(const QDate &date)
{
    for (int i = 0; i < MAX_CHANNELS; i++) {
        m_players[i]->stop();
        m_videoFileLists[i].clear();
    }
    emit canCacheClearRequested();
    
    if (m_videoListWidget) m_videoListWidget->clear();
    
    if (m_videoFolderPath.isEmpty()) return;
    
    qint64 dayStart = timestampFromDate(date);
    qint64 dayEnd = dayStart + 24 * 60 * 60 * 1000;
    QStringList filters;
    filters << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv";
    
    int totalVideos = 0;
    int activeChannels = 0;
    
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        QString channelDir = QString("%1/video%2").arg(m_videoFolderPath).arg(ch);
        QDir dir(channelDir);
        
        if (!dir.exists()) continue;
        
        QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);
        bool chHasVideo = false;
        
        for (const auto &file : fileList) {
            QString baseName = file.baseName();
            bool ok;
            qint64 timestamp = baseName.toLongLong(&ok);
            
            if (ok && timestamp >= dayStart && timestamp < dayEnd) {
                m_videoFileLists[ch].append(file.absoluteFilePath());
                totalVideos++;
                chHasVideo = true;
            }
        }
        if (chHasVideo) activeChannels++;
    }
    
    // 收集所有唯一时间戳并排序，填充到视频列表
    QSet<qint64> uniqueTimestamps;
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        for (const auto &path : m_videoFileLists[ch]) {
            uniqueTimestamps.insert(QFileInfo(path).baseName().toLongLong());
        }
    }
    QList<qint64> sortedTimestamps = uniqueTimestamps.values();
    std::sort(sortedTimestamps.begin(), sortedTimestamps.end());
    
    if (m_videoListWidget) {
        for (qint64 ts : sortedTimestamps) {
            QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(ts);
            int chCount = 0;
            for (int ch = 0; ch < MAX_CHANNELS; ch++) {
                for (const auto &path : m_videoFileLists[ch]) {
                    if (QFileInfo(path).baseName().toLongLong() == ts) {
                        chCount++;
                        break;
                    }
                }
            }
            QString displayText = QString("%1 (%2CH)")
                .arg(dateTime.toString("HH:mm:ss"))
                .arg(chCount);
            auto *item = new QListWidgetItem(displayText, m_videoListWidget);
            item->setData(Qt::UserRole, ts);
        }
    }
    
    if (totalVideos > 0) {
        if (m_videoListWidget && m_videoListWidget->count() > 0) {
            m_videoListWidget->setCurrentRow(0);
        }
        emit statusMessage(tr("找到 %1 个视频文件(跨 %2 通道)").arg(totalVideos).arg(activeChannels));
    } else {
        if (m_videoListWidget) {
            m_videoListWidget->addItem(tr("该日期没有视频文件"));
        }
        emit statusMessage(tr("%1 没有找到视频文件").arg(date.toString("yyyy-MM-dd")));
    }
}

void VideoPlayerManager::playAllChannels(qint64 timestamp)
{
    emit canCacheClearRequested();
    
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        QString bestFile;
        qint64 bestDiff = -1;
        
        for (const auto &path : m_videoFileLists[ch]) {
            qint64 ts = QFileInfo(path).baseName().toLongLong();
            qint64 diff = qAbs(ts - timestamp);
            if (bestDiff < 0 || diff < bestDiff) {
                bestDiff = diff;
                bestFile = path;
            }
        }
        
        if (!bestFile.isEmpty()) {
            m_players[ch]->setSource(QUrl::fromLocalFile(bestFile));
            m_players[ch]->play();
            m_channelLabels[ch]->setText(QString("CH%1 - %2")
                .arg(ch)
                .arg(QFileInfo(bestFile).fileName()));
        } else {
            m_players[ch]->stop();
            m_channelLabels[ch]->setText(QString("CH%1 - 无视频").arg(ch));
        }
    }
    
    emit canLogLoadRequested(timestamp);
}

bool VideoPlayerManager::handleEventFilter(QObject *watched, QEvent *event, QWidget *menuParent)
{
    // 查找是哪个通道的容器
    int channel = -1;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (watched == m_channelContainers[i]) {
            channel = i;
            break;
        }
    }
    if (channel < 0) {
        return false;
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
        
        auto *switchMenu = menu.addMenu(tr("切换到通道"));
        for (int i = 0; i < MAX_CHANNELS; i++) {
            QString label = QString("CH%1").arg(i);
            label += !m_videoFileLists[i].isEmpty() ? tr(" - 有视频") : tr(" - 无视频");
            QAction *action = switchMenu->addAction(label);
            action->setData(i);
            if (i == m_activeChannel && m_viewMode == View1) {
                action->setCheckable(true);
                action->setChecked(true);
            }
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
            m_activeChannel = ch;
            setViewMode(View1);
        } else if (selected == act1) {
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
