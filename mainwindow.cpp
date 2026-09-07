#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "simple_table_model.h"
#include "can_data_manager.h"
#include "video_player_manager.h"
#include "audio_player_manager.h"

#include <QCalendarWidget>
#include <QCoreApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSignalBlocker>
#include <QSet>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDateTime>
#include <QDialog>
#include <QTimer>
#include <QMediaPlayer>
#include <QUrl>
#include <QDebug>

#include <algorithm>
#include <limits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 临时测试代码（可保留或移除，已确认媒体加载正常）
    //QMediaPlayer *testPlayer = new QMediaPlayer(this);
    //testPlayer->setSource(QUrl::fromLocalFile("/home/user/Data_replay/视频文件/1777291427673.mp4"));
    //connect(testPlayer, &QMediaPlayer::mediaStatusChanged, [](QMediaPlayer::MediaStatus status) {
        //qDebug() << "Test player status:" << status;
    //});
    //connect(testPlayer, &QMediaPlayer::errorOccurred, [](QMediaPlayer::Error error, const QString &errorString) {
        //qDebug() << "Test player error:" << error << errorString;
    //});

    m_videoManager = new VideoPlayerManager(this);
    m_audioManager = new AudioPlayerManager(this);
    m_canManager = new CanDataManager(this);

    buildUi();
    connectSignals();
    initializeDefaultFolders();

    m_videoManager->setViewMode(VideoPlayerManager::View4);

    m_seekDebounceTimer = new QTimer(this);
    m_seekDebounceTimer->setSingleShot(true);
    m_seekDebounceTimer->setInterval(500);
    connect(m_seekDebounceTimer, &QTimer::timeout, this, [this]() {
        m_canManager->setLastSeekPositionMs(-1);
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Data Replay"));
    resize(1800, 900);
    setMinimumSize(1400, 700);

    m_videoManager->createPlayers(this);

    auto *videoGridWidget = new QWidget(this);
    videoGridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoManager->setVideoGridWidget(videoGridWidget);

    m_calendarWidget = new QCalendarWidget(this);
    m_calendarWidget->setSelectedDate(QDate::currentDate());
    m_calendarWidget->setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
    m_calendarWidget->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader); 
    m_calendarWidget->setStyleSheet("QCalendarWidget QWidget { font-size: 10px; }");
    m_calendarWidget->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendarWidget->setGridVisible(true);
    m_currentDate = QDate::currentDate();

    m_openVideoFolderButton = new QPushButton(tr("选择视频文件夹"), this);
    m_openAudioFolderButton = new QPushButton(tr("选择音频文件夹"), this);
    m_openCanLogFolderButton = new QPushButton(tr("选择CAN Log文件夹"), this);
    m_videoFolderLabel = new QLabel(tr("未选择视频文件夹"), this);
    m_videoFolderLabel->setWordWrap(true);
    m_videoFolderLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");
    m_audioFolderLabel = new QLabel(tr("未选择音频文件夹"), this);
    m_audioFolderLabel->setWordWrap(true);
    m_audioFolderLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");

    m_videoListWidget = new QListWidget(this);
    m_videoListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    // 添加样式表，确保列表项清晰可点击
    m_videoListWidget->setStyleSheet(
        "QListWidget::item { color: black; }"
        "QListWidget::item:selected { background-color: #3399FF; color: white; }"
    );
    m_videoManager->setVideoListWidget(m_videoListWidget);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->setSpacing(4);
    leftLayout->addWidget(m_openVideoFolderButton);
    leftLayout->addWidget(m_openAudioFolderButton);
    leftLayout->addWidget(m_openCanLogFolderButton);
    leftLayout->addWidget(m_videoFolderLabel);
    leftLayout->addWidget(m_audioFolderLabel);
    leftLayout->addWidget(new QLabel(tr("视频列表:"), this));
    leftLayout->addWidget(m_videoListWidget, 1);
    leftLayout->addWidget(m_calendarWidget);

    auto *leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);
    leftWidget->setMinimumWidth(220);
    leftWidget->setMaximumWidth(320);

    // ===== 中间面板 =====
    m_playButton = new QPushButton(tr("播放"), this);

    m_speedComboBox = new QComboBox(this);
    m_speedComboBox->addItem("x0.01", 0.01);
    m_speedComboBox->addItem("x0.05", 0.05);
    m_speedComboBox->addItem("x0.1", 0.1);
    m_speedComboBox->addItem("x0.25", 0.25);
    m_speedComboBox->addItem("x0.5", 0.5);
    m_speedComboBox->addItem("x1.0", 1.0);
    m_speedComboBox->addItem("x1.5", 1.5);
    m_speedComboBox->addItem("x2.0", 2.0);
    m_speedComboBox->setCurrentIndex(5);
    m_speedComboBox->setMaximumWidth(80);

    m_rewind30Button = new QPushButton(tr("-30s"), this);
    m_rewind15Button = new QPushButton(tr("-15s"), this);
    m_rewind5Button = new QPushButton(tr("-5s"), this);
    m_rewind1Button = new QPushButton(tr("-1s"), this);
    m_forward1Button = new QPushButton(tr("+1s"), this);
    m_forward5Button = new QPushButton(tr("+5s"), this);
    m_forward15Button = new QPushButton(tr("+15s"), this);
    m_forward30Button = new QPushButton(tr("+30s"), this);

    m_timelineWidget = new ReplayTimelineWidget(this);

    m_timeLabel = new QLabel(tr("00:00:00.000 / 24:00:00.000"), this);
    m_timeLabel->setMinimumWidth(220);

    m_timestampLabel = new QLabel(tr("时间戳: --"), this);
    m_timestampLabel->setMinimumWidth(350);

    m_canUpdateTimer = new QTimer(this);
    m_canUpdateTimer->setInterval(500);

    m_clockUpdateTimer = new QTimer(this);
    m_clockUpdateTimer->setInterval(50);
    m_baseTimestampMs = timestampFromDate(m_currentDate);
    m_durationMs = 24LL * 60 * 60 * 1000;
    m_replayClock.setBaseTimestampMs(m_baseTimestampMs);

    auto *centerLayout = new QVBoxLayout;
    centerLayout->setSpacing(4);
    centerLayout->addWidget(videoGridWidget, 1);
    centerLayout->addWidget(m_timelineWidget);

    auto *infoLayout = new QHBoxLayout;
    infoLayout->addWidget(m_timestampLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_timeLabel);
    centerLayout->addLayout(infoLayout);

    auto *playControlLayout = new QHBoxLayout;
    playControlLayout->addStretch();
    playControlLayout->addWidget(m_rewind30Button);
    playControlLayout->addWidget(m_rewind15Button);
    playControlLayout->addWidget(m_rewind5Button);
    playControlLayout->addWidget(m_rewind1Button);
    playControlLayout->addWidget(m_playButton);
    playControlLayout->addWidget(m_forward1Button);
    playControlLayout->addWidget(m_forward5Button);
    playControlLayout->addWidget(m_forward15Button);
    playControlLayout->addWidget(m_forward30Button);
    playControlLayout->addWidget(new QLabel(tr("倍速:"), this));
    playControlLayout->addWidget(m_speedComboBox);
    playControlLayout->addStretch();
    centerLayout->addLayout(playControlLayout);

    auto *centerWidget = new QWidget(this);
    centerWidget->setLayout(centerLayout);

    // ===== 右侧CAN数据面板 =====
    m_canStatusLabel = new QLabel(tr("未加载 CAN Log"), this);

    m_showRawCanFramesButton = new QPushButton(tr("查看当前原始帧"), this);

    m_canTable = new QTableView(this);
    m_canTableModel = new SimpleTableModel({tr("时间戳"), tr("信号名称"), tr("值(信号来源)"), tr("单位")}, this);
    m_canTable->setModel(m_canTableModel);
    m_canTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canTable->horizontalHeader()->setStretchLastSection(false);
    m_canTable->setColumnWidth(0, 180);
    m_canTable->setColumnWidth(1, 150);
    m_canTable->setColumnWidth(2, 130);
    m_canTable->setColumnWidth(3, 60);
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canTable->setAlternatingRowColors(true);

    m_canRawTable = new QTableView(this);
    m_canRawTableModel = new SimpleTableModel({tr("原始时间戳"), tr("CAN ID"), tr("数据")}, this);
    m_canRawTable->setModel(m_canRawTableModel);
    m_canRawTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canRawTable->horizontalHeader()->setStretchLastSection(false);
    m_canRawTable->setColumnWidth(0, 150);
    m_canRawTable->setColumnWidth(1, 90);
    m_canRawTable->setColumnWidth(2, 190);
    m_canRawTable->verticalHeader()->setVisible(false);
    m_canRawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canRawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canRawTable->setAlternatingRowColors(true);

    m_canManager->setCanTables(m_canTable, m_canRawTable, m_canTableModel, m_canRawTableModel);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_canStatusLabel);
    rightLayout->addWidget(m_showRawCanFramesButton);
    rightLayout->addWidget(m_canTable, 1);
    rightLayout->addWidget(m_canRawTable, 1);

    m_canSidebarWidget = new QWidget(this);
    m_canSidebarWidget->setLayout(rightLayout);
    m_canSidebarWidget->setMinimumWidth(250);

    m_canSidebarEdgeButton = new QPushButton(tr("<"), this);
    m_canSidebarEdgeButton->setFixedSize(24, 72);
    m_canSidebarEdgeButton->setCursor(Qt::PointingHandCursor);
    m_canSidebarEdgeButton->setToolTip(tr("隐藏CAN数据"));
    m_canSidebarEdgeButton->setStyleSheet(
        "QPushButton {"
        " border: 1px solid #b8bcc8;"
        " border-radius: 4px;"
        " background: transparent;"
        " color: #40444f;"
        " font-size: 18px;"
        " font-weight: bold;"
        "}"
        "QPushButton:hover { background: #eef0f4; }"
        "QPushButton:pressed { background: #dde1e8; }");

    auto *toggleStripLayout = new QVBoxLayout;
    toggleStripLayout->setContentsMargins(0, 0, 0, 0);
    toggleStripLayout->setSpacing(0);
    toggleStripLayout->addStretch();
    toggleStripLayout->addWidget(m_canSidebarEdgeButton, 0, Qt::AlignHCenter);
    toggleStripLayout->addStretch();

    m_canSidebarToggleStrip = new QWidget(this);
    m_canSidebarToggleStrip->setFixedWidth(28);
    m_canSidebarToggleStrip->setLayout(toggleStripLayout);
    m_canSidebarToggleStrip->setStyleSheet("QWidget { background: transparent; }");

    m_mainSplitter = new QSplitter(this);
    m_mainSplitter->addWidget(leftWidget);
    m_mainSplitter->addWidget(centerWidget);
    m_mainSplitter->addWidget(m_canSidebarWidget);
    m_mainSplitter->addWidget(m_canSidebarToggleStrip);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 5);
    m_mainSplitter->setStretchFactor(2, 2);
    m_mainSplitter->setStretchFactor(3, 0);
    QList<int> sizes;
    sizes << 290 << 990 << 400 << 28;
    m_mainSplitter->setSizes(sizes);
    m_canSidebarWidget->setVisible(true);

    setCentralWidget(m_mainSplitter);
    setStatus(tr("请选择视频文件夹和 CAN Log文件夹，然后选择日期加载视频列表"));
}

void MainWindow::connectSignals()
{
    connect(m_openVideoFolderButton, &QPushButton::clicked, this, &MainWindow::openVideoFolder);
    connect(m_openAudioFolderButton, &QPushButton::clicked, this, &MainWindow::openAudioFolder);
    connect(m_openCanLogFolderButton, &QPushButton::clicked, this, &MainWindow::openCanLogFolder);

    connect(m_videoListWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        QListWidgetItem *item = m_videoListWidget->item(row);
        if (!item) return;
        qint64 timestamp = item->data(Qt::UserRole).toLongLong();
        if (timestamp > 0) {
            const int selectedChannel = m_videoManager->channelForTimestamp(timestamp);
            if (selectedChannel >= 0) {
                m_videoManager->showSingleChannel(selectedChannel);
            }

            m_baseTimestampMs = timestampFromDate(m_currentDate);
            qint64 dayOffsetMs = qMax<qint64>(0, timestamp - m_baseTimestampMs);
            m_replayClock.setBaseTimestampMs(m_baseTimestampMs);
            m_replayClock.seek(dayOffsetMs);
            m_replayClock.setPlaybackRate(m_speedComboBox->currentData().toDouble());
            m_replayClock.play();
            m_canManager->setBaseTimestampMs(m_baseTimestampMs);
            m_videoManager->playAllChannels(timestamp);
            m_audioManager->syncToTimestamp(timestamp, 0, true);
            m_timelineWidget->setPositionMs(dayOffsetMs);
            setPlaybackUi(true);
        }
    });

    connect(m_calendarWidget, &QCalendarWidget::selectionChanged, this, [this]() {
        QDate newDate = m_calendarWidget->selectedDate();
        if (newDate != m_currentDate) {
            m_currentDate = newDate;
            onDateChanged(newDate);
        }
    });

    for (int i = 0; i < VideoPlayerManager::MAX_CHANNELS; i++) {
        m_videoManager->channelContainer(i)->installEventFilter(this);
        m_videoManager->videoWidget(i)->installEventFilter(this);
    }

    QMediaPlayer *masterPlayer = m_videoManager->masterPlayer();
    connect(masterPlayer, &QMediaPlayer::durationChanged, this, &MainWindow::updateDuration);
    connect(masterPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });

    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        qreal rate = m_speedComboBox->itemData(index).toDouble();
        m_replayClock.setPlaybackRate(rate);
        m_videoManager->setPlaybackRate(rate);
        m_audioManager->setPlaybackRate(rate);
        setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
    });

    connect(m_canUpdateTimer, &QTimer::timeout, this, [this]() {
        if (!m_replayClock.isPlaying()) {
            qint64 displayPos = (m_canManager->lastSeekPositionMs() >= 0)
                ? m_canManager->lastSeekPositionMs()
                : m_replayClock.currentOffsetMs();
            m_canManager->updateCanDisplay(displayPos);
        }
    });
    m_canUpdateTimer->start();

    connect(m_clockUpdateTimer, &QTimer::timeout, this, &MainWindow::updateFromReplayClock);
    m_clockUpdateTimer->start();

    connect(m_timelineWidget, &ReplayTimelineWidget::positionSelected,
            this, &MainWindow::seekOnTimeline);

    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(m_showRawCanFramesButton, &QPushButton::clicked, this, &MainWindow::showRawCanFramesDialog);
    auto connectSeekButton = [this](QPushButton *button, qint64 deltaMs) {
        connect(button, &QPushButton::clicked, this, [this, deltaMs]() {
            const qint64 targetMs = m_replayClock.currentOffsetMs() + deltaMs;
            seekOnTimeline(qBound<qint64>(0, targetMs, m_durationMs));
        });
    };
    connectSeekButton(m_rewind30Button, -30000);
    connectSeekButton(m_rewind15Button, -15000);
    connectSeekButton(m_rewind5Button, -5000);
    connectSeekButton(m_rewind1Button, -1000);
    connectSeekButton(m_forward1Button, 1000);
    connectSeekButton(m_forward5Button, 5000);
    connectSeekButton(m_forward15Button, 15000);
    connectSeekButton(m_forward30Button, 30000);

    connect(m_canSidebarEdgeButton, &QPushButton::clicked, this, [this]() {
        if (!m_canSidebarWidget || !m_mainSplitter) return;
        const bool showSidebar = !m_canSidebarWidget->isVisible();
        m_canSidebarWidget->setVisible(showSidebar);
        m_canSidebarEdgeButton->setText(showSidebar ? tr("<") : tr(">"));
        m_canSidebarEdgeButton->setToolTip(showSidebar ? tr("隐藏CAN数据") : tr("显示CAN数据"));
        if (showSidebar) {
            QList<int> sizes = m_mainSplitter->sizes();
            if (sizes.size() == 4 && sizes[2] <= 0) {
                sizes[2] = 400;
                if (sizes[1] > 400) sizes[1] -= 400;
                sizes[3] = 28;
                m_mainSplitter->setSizes(sizes);
            }
        }
    });

    connect(m_videoManager, &VideoPlayerManager::canCacheClearRequested, this, [this]() {
        m_canManager->clearCanCache();
    });
    connect(m_videoManager, &VideoPlayerManager::canLogLoadRequested, this, [this](qint64 timestamp) {
        if (!m_canManager->canLogFolderPath().isEmpty()) {
            m_canManager->loadCanLogByTimestamp(timestamp);
        }
    });
    connect(m_videoManager, &VideoPlayerManager::statusMessage, this, [this](const QString &msg) {
        setStatus(msg);
    });
    connect(m_videoManager, &VideoPlayerManager::videoListLoaded, this, [this](bool hasVideos) {
        m_timelineWidget->setAvailabilityRanges(
            m_videoManager->availabilityRangesForDate(m_currentDate));
        if (hasVideos && m_videoListWidget && m_videoListWidget->count() > 0) {
            m_videoListWidget->setCurrentRow(0);
        }
    });
    connect(m_canManager, &CanDataManager::statusMessage, this, [this](const QString &msg) {
        m_canStatusLabel->setText(msg);
    });
}

void MainWindow::togglePlayback()
{
    if (m_videoManager->videoFolderPath().isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择视频文件夹"));
        return;
    }
    if (m_canManager->canLogFolderPath().isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择CAN Log文件夹"));
        return;
    }

    bool isPlaying = m_replayClock.isPlaying();

    if (isPlaying) {
        m_replayClock.pause();
        m_videoManager->pauseAll();
        m_audioManager->pause();
        updateFromReplayClock();
    } else {
        bool hasVideo = false;
        for (int i = 0; i < VideoPlayerManager::MAX_CHANNELS; i++) {
            if (m_videoManager->hasVideo(i)) {
                hasVideo = true;
                break;
            }
        }
        if (!hasVideo) {
            QMessageBox::warning(this, tr("提示"), tr("没有可播放的视频"));
            return;
        }
        m_canManager->setLastCanUpdateMs(-1);
        m_replayClock.play();
        m_videoManager->syncPlayersToTimestamp(m_replayClock.currentTimestampMs(), 0, true);
        m_audioManager->syncToTimestamp(m_replayClock.currentTimestampMs(), 0, true);
    }
    setPlaybackUi(m_replayClock.isPlaying());
}

void MainWindow::setPlaybackRate(qreal rate)
{
    m_replayClock.setPlaybackRate(rate);
    m_videoManager->setPlaybackRate(rate);
    m_audioManager->setPlaybackRate(rate);
    setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
}

void MainWindow::seekOnTimeline(qint64 positionMs)
{
    m_replayClock.seek(positionMs);
    m_videoManager->syncPlayersToTimestamp(m_replayClock.currentTimestampMs(), 0, m_replayClock.isPlaying());
    m_audioManager->syncToTimestamp(m_replayClock.currentTimestampMs(), 0, m_replayClock.isPlaying());

    bool inRange = false;
    if (m_canManager->hasCanData()) {
        qint64 firstAbsolute = m_baseTimestampMs + m_canManager->firstFrameTimeMs();
        qint64 lastAbsolute = m_baseTimestampMs + m_canManager->lastFrameTimeMs();
        qint64 currentAbsolute = m_baseTimestampMs + positionMs;
        inRange = (currentAbsolute >= firstAbsolute && currentAbsolute <= lastAbsolute);
    }

    if (!inRange) {
        m_canManager->loadCanLogByTimestamp(m_baseTimestampMs, positionMs);
    } else {
        m_canManager->updateCanDisplay(positionMs);
    }

    m_canManager->setLastSeekPositionMs(positionMs);
    m_canManager->setLastCanUpdateMs(-1);
    m_seekDebounceTimer->start();
    updatePosition(positionMs);
}

void MainWindow::updateFromReplayClock()
{
    qint64 offsetMs = m_replayClock.currentOffsetMs();
    if (m_durationMs > 0 && offsetMs > m_durationMs) {
        offsetMs = m_durationMs;
        m_replayClock.seek(offsetMs);
        m_replayClock.pause();
        m_videoManager->pauseAll();
        m_audioManager->pause();
        setPlaybackUi(false);
    }

    m_videoManager->syncPlayersToTimestamp(
        m_replayClock.currentTimestampMs(),
        m_replayClock.isPlaying() ? 150 : 0,
        m_replayClock.isPlaying());
    m_audioManager->syncToTimestamp(
        m_replayClock.currentTimestampMs(),
        m_replayClock.isPlaying() ? 150 : 0,
        m_replayClock.isPlaying());

    updatePosition(offsetMs);
}

void MainWindow::updatePosition(qint64 positionMs)
{
    m_timelineWidget->setPositionMs(positionMs);

    if (m_baseTimestampMs > 0) {
        qint64 currentTimestamp = m_baseTimestampMs + positionMs;
        m_timeLabel->setText(QString("%1 / 24:00:00.000")
            .arg(CanDataManager::formatTime(positionMs)));
        m_timestampLabel->setText(CanDataManager::formatDateTime(currentTimestamp));
    } else {
        m_timeLabel->setText(CanDataManager::formatTime(positionMs) + " / 24:00:00.000");
        m_timestampLabel->setText(tr("时间戳: --"));
    }

    qint64 checkTimestamp = (m_canManager->lastSeekPositionMs() >= 0)
        ? (m_baseTimestampMs + m_canManager->lastSeekPositionMs())
        : (m_baseTimestampMs + positionMs);
    m_canManager->loadNextCanLogIfNeeded(checkTimestamp);

    qreal rate = m_speedComboBox->currentData().toDouble();
    if (rate <= 0) rate = 1.0;
    qint64 thresholdMs = qMax(5LL, static_cast<qint64>(500 * rate));

    qint64 displayPositionMs = (m_canManager->lastSeekPositionMs() >= 0)
        ? m_canManager->lastSeekPositionMs()
        : positionMs;

    if (displayPositionMs - m_canManager->lastCanUpdateMs() >= thresholdMs || m_canManager->lastCanUpdateMs() == -1) {
        m_canManager->setLastCanUpdateMs(displayPositionMs);
        m_canManager->updateCanDisplay(displayPositionMs);
    }

    if (m_rawCanFramesDialog && m_rawCanFramesDialog->isVisible()) {
        refreshRawCanFramesDialog(positionMs);
    }
}

void MainWindow::updateDuration(qint64 durationMs)
{
    Q_UNUSED(durationMs);
    m_durationMs = 24LL * 60 * 60 * 1000;
}

void MainWindow::showRawCanFramesDialog()
{
    m_replayClock.pause();
    m_videoManager->pauseAll();
    m_audioManager->pause();
    updateFromReplayClock();
    setPlaybackUi(false);
    m_playButton->setEnabled(false);

    const qint64 positionMs = (m_canManager->lastSeekPositionMs() >= 0)
        ? m_canManager->lastSeekPositionMs()
        : m_replayClock.currentOffsetMs();

    auto *dialog = new QDialog(this);
    m_rawCanFramesDialog = dialog;
    m_lastRawCanFramesDialogPositionMs = -1;
    QFont dialogFont = dialog->font();
    dialogFont.setPointSize(qMax(11, dialogFont.pointSize() + 2));
    dialog->setFont(dialogFont);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("当前时间戳前后 CAN 原始帧"));
    dialog->resize(1350, 720);
    connect(dialog, &QObject::destroyed, this, [this]() {
        m_playButton->setEnabled(true);
        m_rawCanFramesDialog = nullptr;
        m_rawCanFramesInfoLabel = nullptr;
        m_rawCanFramesTable = nullptr;
        m_rawCanIdFilter = nullptr;
        m_lastRawCanFramesDialogPositionMs = -1;
    });

    auto *layout = new QVBoxLayout(dialog);
    m_rawCanFramesInfoLabel = new QLabel(dialog);
    layout->addWidget(m_rawCanFramesInfoLabel);

    auto *stepLayout = new QHBoxLayout;
    stepLayout->addStretch();
    auto addRawFrameStepButton = [this, stepLayout, dialog](const QString &text, qint64 deltaMs) {
        auto *button = new QPushButton(text, dialog);
        stepLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, deltaMs]() {
            const qint64 targetMs = qBound<qint64>(0, m_replayClock.currentOffsetMs() + deltaMs, m_durationMs);
            seekOnTimeline(targetMs);
            m_replayClock.pause();
            m_videoManager->pauseAll();
            m_audioManager->pause();
            setPlaybackUi(false);
        });
    };
    addRawFrameStepButton(tr("-300ms"), -300);
    addRawFrameStepButton(tr("-200ms"), -200);
    addRawFrameStepButton(tr("-100ms"), -100);
    addRawFrameStepButton(tr("+100ms"), 100);
    addRawFrameStepButton(tr("+200ms"), 200);
    addRawFrameStepButton(tr("+300ms"), 300);
    stepLayout->addStretch();
    layout->addLayout(stepLayout);

    auto *filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel(tr("CAN ID 筛选:"), dialog));
    m_rawCanIdFilter = new QComboBox(dialog);
    m_rawCanIdFilter->addItem(tr("全部"), QString());
    m_rawCanIdFilter->setMinimumWidth(180);
    m_rawCanIdFilter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_rawCanIdFilter->setMinimumContentsLength(14);
    filterLayout->addWidget(m_rawCanIdFilter);
    filterLayout->addStretch();
    layout->addLayout(filterLayout);

    m_rawCanFramesTable = new QTreeWidget(dialog);
    m_rawCanFramesTable->setColumnCount(4);
    m_rawCanFramesTable->setHeaderLabels({tr("解析时间戳"), tr("原始时间戳"), tr("CAN ID"), tr("详细内容")});
    m_rawCanFramesTable->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_rawCanFramesTable->header()->setStretchLastSection(false);
    m_rawCanFramesTable->setColumnWidth(0, 180);
    m_rawCanFramesTable->setColumnWidth(1, 160);
    m_rawCanFramesTable->setColumnWidth(2, 90);
    m_rawCanFramesTable->setColumnWidth(3, 650);
    m_rawCanFramesMaximumWidths = {180, 160, 90, 650};
    m_rawCanFramesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rawCanFramesTable->setAlternatingRowColors(true);
    m_rawCanFramesTable->setRootIsDecorated(true);
    m_rawCanFramesTable->setUniformRowHeights(true);
    layout->addWidget(m_rawCanFramesTable, 1);

    connect(m_rawCanIdFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_rawCanFramesTable || !m_rawCanIdFilter) return;
        const QString selectedId = m_rawCanIdFilter->currentData().toString();
        for (int i = 0; i < m_rawCanFramesTable->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_rawCanFramesTable->topLevelItem(i);
            item->setHidden(!selectedId.isEmpty() && item->text(2) != selectedId);
        }
    });

    connect(m_rawCanFramesTable, &QTreeWidget::itemClicked, this, [](QTreeWidgetItem *item, int column) {
        if (!item) return;
        // 第 0 列左侧箭头由 QTreeWidget 自己处理；点击其余父项区域也可展开/收起。
        if (!item->parent() && item->childCount() > 0 && column != 0) {
            item->setExpanded(!item->isExpanded());
        }
    });

    connect(m_rawCanFramesTable, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item) return;
        QTreeWidgetItem *frameItem = item->parent() ? item->parent() : item;
        const QString parsedDateTime = frameItem->text(0);
        const QString parsedTime = parsedDateTime.section('|', 1).trimmed();
        const QTime parsed = QTime::fromString(parsedTime, "HH:mm:ss.zzz");
        if (!parsed.isValid()) return;

        seekOnTimeline(QTime(0, 0).msecsTo(parsed));
        m_replayClock.pause();
        m_videoManager->pauseAll();
        m_audioManager->pause();
        setPlaybackUi(false);
    });

    refreshRawCanFramesDialog(positionMs);
    dialog->show();
}

void MainWindow::refreshRawCanFramesDialog(qint64 positionMs)
{
    if (!m_rawCanFramesDialog || !m_rawCanFramesTable || !m_rawCanFramesInfoLabel) return;
    // 暂停状态下主时钟仍会周期更新界面；同一位置不重复重建树，避免展开项被收回。
    if (positionMs == m_lastRawCanFramesDialogPositionMs) return;
    m_lastRawCanFramesDialogPositionMs = positionMs;

    QVector<QStringList> rows = m_canManager->rawFrameRowsAround(positionMs, 500);
    QString currentOriginalTimestamp = tr("无");
    qint64 bestDiff = -1;
    for (const QStringList &row : rows) {
        if (row.size() < 2) continue;
        const QTime parsed = QTime::fromString(row[0].section('|', 1).trimmed(), "HH:mm:ss.zzz");
        if (!parsed.isValid()) continue;
        const qint64 rowTime = QTime(0, 0).msecsTo(parsed);
        const qint64 diff = qAbs(rowTime - positionMs);
        if (bestDiff < 0 || diff < bestDiff) {
            bestDiff = diff;
            currentOriginalTimestamp = row[1];
        }
    }

    const QString selectedId = m_rawCanIdFilter ? m_rawCanIdFilter->currentData().toString() : QString();
    QSet<QString> canIds;
    m_rawCanFramesTable->clear();
    QTreeWidgetItem *currentParent = nullptr;
    QString currentCanId;
    qint64 groupedOriginalTimestamp = -1;
    for (const QStringList &row : rows) {
        if (row.size() < 5) continue;
        canIds.insert(row[2]);
        bool timestampOk = false;
        const qint64 originalTimestamp = row[1].toLongLong(&timestampOk);
        // 原始帧与紧随其后的解析帧偶尔会相差 1ms。相同 CAN ID 在 5ms
        // 内视为同一帧；正常报文周期远大于该容差，不会误合并相邻帧。
        const bool sameFrame = currentParent && timestampOk &&
            row[2] == currentCanId && groupedOriginalTimestamp >= 0 &&
            qAbs(originalTimestamp - groupedOriginalTimestamp) <= 5;
        if (!sameFrame) {
            currentCanId = row[2];
            groupedOriginalTimestamp = timestampOk ? originalTimestamp : -1;
            currentParent = new QTreeWidgetItem(m_rawCanFramesTable, row.mid(0, 3));
            currentParent->setText(3, tr("点击展开"));
            if (!row[3].isEmpty()) {
                auto *rawChild = new QTreeWidgetItem(currentParent);
                rawChild->setText(3, tr("原始数据: %1").arg(row[3]));
            }
        }
        if (!row[4].isEmpty()) {
            auto *child = new QTreeWidgetItem(currentParent);
            child->setText(3, row[4]);
        }
        currentParent->setText(3, tr("点击展开 (%1)").arg(currentParent->childCount()));
    }

    if (m_rawCanIdFilter) {
        QStringList sortedIds = canIds.values();
        std::sort(sortedIds.begin(), sortedIds.end());
        const QSignalBlocker blocker(m_rawCanIdFilter);
        m_rawCanIdFilter->clear();
        m_rawCanIdFilter->addItem(tr("全部"), QString());
        for (const QString &canId : sortedIds) m_rawCanIdFilter->addItem(canId, canId);
        const int selectedIndex = selectedId.isEmpty() ? 0 : m_rawCanIdFilter->findData(selectedId);
        m_rawCanIdFilter->setCurrentIndex(qMax(0, selectedIndex));
    }
    const QString activeId = m_rawCanIdFilter ? m_rawCanIdFilter->currentData().toString() : QString();
    for (int i = 0; i < m_rawCanFramesTable->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_rawCanFramesTable->topLevelItem(i);
        item->setHidden(!activeId.isEmpty() && item->text(2) != activeId);
    }

    QVector<QStringList> widthRows;
    widthRows.reserve(rows.size() * 2);
    for (const QStringList &row : rows) {
        if (row.size() < 5) continue;
        if (!row[3].isEmpty()) widthRows.append({row[0], row[1], row[2], tr("原始数据: %1").arg(row[3])});
        if (!row[4].isEmpty()) widthRows.append({row[0], row[1], row[2], row[4]});
    }
    growRawCanFramesColumnWidths(widthRows);
    m_rawCanFramesInfoLabel->setText(tr("解析时间戳: %1 | 原始时间戳: %2 | 显示前后500ms内原始帧%3")
        .arg(CanDataManager::formatDateTime(m_baseTimestampMs + positionMs))
        .arg(currentOriginalTimestamp)
        .arg(rows.isEmpty() ? tr(" | 无 CAN 原始帧") : QString()));
}

void MainWindow::growRawCanFramesColumnWidths(const QVector<QStringList> &rows)
{
    if (!m_rawCanFramesTable || rows.isEmpty()) return;

    const QFontMetrics metrics(m_rawCanFramesTable->font());
    for (const QStringList &row : rows) {
        if (m_rawCanFramesMaximumWidths.size() < row.size()) {
            m_rawCanFramesMaximumWidths.resize(row.size());
        }
        for (int column = 0; column < row.size(); ++column) {
            const int requiredWidth = metrics.horizontalAdvance(row[column]) + 24;
            if (requiredWidth <= m_rawCanFramesMaximumWidths[column]) continue;

            m_rawCanFramesMaximumWidths[column] = requiredWidth;
            m_rawCanFramesTable->setColumnWidth(
                column,
                qMax(m_rawCanFramesTable->columnWidth(column), requiredWidth));
        }
    }
}

void MainWindow::setPlaybackUi(bool playing)
{
    m_playButton->setText(playing ? tr("暂停") : tr("播放"));
}

void MainWindow::setStatus(const QString &message)
{
    statusBar()->showMessage(message);
}

void MainWindow::initializeDefaultFolders()
{
    const QString videoPath = defaultVideoFolderPath();
    if (QDir(videoPath).exists()) {
        m_videoManager->setVideoFolderPath(videoPath);
        m_videoFolderLabel->setText(videoPath);
        m_videoFolderLabel->setStyleSheet("QLabel { color: black; font-size: 11px; }");
        loadVideoList(m_currentDate);
    }

    const QString canLogPath = defaultCanLogFolderPath();
    if (QDir(canLogPath).exists()) {
        m_canManager->setCanLogFolderPath(canLogPath);
        m_canManager->loadCanLogFolder();
    }

    const QString audioPath = defaultAudioFolderPath();
    if (QDir(audioPath).exists()) {
        m_audioManager->setAudioFolderPath(audioPath);
        m_audioManager->loadForDate(m_currentDate);
        m_audioFolderLabel->setText(audioPath);
        m_audioFolderLabel->setStyleSheet("QLabel { color: black; font-size: 11px; }");
    }

    if (QDir(videoPath).exists() || QDir(canLogPath).exists()) {
        setStatus(tr("已加载默认路径：视频=%1 | CAN Log=%2").arg(videoPath, canLogPath));
    }
}

void MainWindow::openVideoFolder()
{
    const QString startPath = m_videoManager->videoFolderPath().isEmpty()
        ? defaultVideoFolderPath()
        : m_videoManager->videoFolderPath();
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择视频文件夹"), startPath);
    if (folderPath.isEmpty()) return;

    m_videoManager->setVideoFolderPath(folderPath);
    m_videoFolderLabel->setText(folderPath);
    m_videoFolderLabel->setStyleSheet("QLabel { color: black; font-size: 11px; }");
    setStatus(tr("已选择视频文件夹: %1").arg(folderPath));

    loadVideoList(m_currentDate);
}

void MainWindow::openCanLogFolder()
{
    const QString startPath = m_canManager->canLogFolderPath().isEmpty()
        ? defaultCanLogFolderPath()
        : m_canManager->canLogFolderPath();
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择CAN Log文件夹"), startPath);
    if (folderPath.isEmpty()) return;

    m_canManager->setCanLogFolderPath(folderPath);
    m_canManager->loadCanLogFolder();
    setStatus(tr("已选择CAN Log文件夹: %1").arg(folderPath));

    if (m_baseTimestampMs > 0 && !m_canManager->canLogFileList().isEmpty()) {
        qint64 currentPosition = m_replayClock.currentOffsetMs();
        m_canManager->loadCanLogByTimestamp(m_baseTimestampMs, currentPosition);
    }
}

void MainWindow::openAudioFolder()
{
    const QString startPath = m_audioManager->audioFolderPath().isEmpty()
        ? defaultAudioFolderPath() : m_audioManager->audioFolderPath();
    const QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择音频文件夹"), startPath);
    if (folderPath.isEmpty()) return;

    m_audioManager->setAudioFolderPath(folderPath);
    m_audioManager->loadForDate(m_currentDate);
    m_audioFolderLabel->setText(folderPath);
    m_audioFolderLabel->setStyleSheet("QLabel { color: black; font-size: 11px; }");
    setStatus(tr("已选择音频文件夹: %1，找到 %2 个音频文件")
        .arg(folderPath).arg(m_audioManager->audioFileCount()));
    m_audioManager->syncToTimestamp(m_replayClock.currentTimestampMs(), 0, m_replayClock.isPlaying());
}

void MainWindow::onDateChanged(const QDate &date)
{
    m_baseTimestampMs = timestampFromDate(date);
    m_durationMs = 24LL * 60 * 60 * 1000;
    m_replayClock.pause();
    m_replayClock.setBaseTimestampMs(m_baseTimestampMs);
    m_replayClock.seek(0);
    m_canManager->setBaseTimestampMs(m_baseTimestampMs);
    m_timelineWidget->setPositionMs(0);
    m_audioManager->loadForDate(date);
    setPlaybackUi(false);

    if (!m_videoManager->videoFolderPath().isEmpty()) {
        loadVideoList(date);
    }

}

void MainWindow::loadVideoList(const QDate &date)
{
    m_videoManager->loadAllChannels(date);
}

QDate MainWindow::dateFromTimestamp(qint64 timestampMs) const
{
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    return dateTime.date();
}

qint64 MainWindow::timestampFromDate(const QDate &date) const
{
    QDateTime dateTime(date, QTime(0, 0, 0));
    return dateTime.toMSecsSinceEpoch();
}

QString MainWindow::defaultVideoFolderPath() const
{
    return QDir(qEnvironmentVariable("DATA_REPLAY_HOME", QCoreApplication::applicationDirPath())).filePath(QString::fromUtf8("视频文件"));
}

QString MainWindow::defaultCanLogFolderPath() const
{
    return QDir(qEnvironmentVariable("DATA_REPLAY_HOME", QCoreApplication::applicationDirPath())).filePath(QString::fromUtf8("log文件"));
}

QString MainWindow::defaultAudioFolderPath() const
{
    return QDir(qEnvironmentVariable("DATA_REPLAY_HOME", QCoreApplication::applicationDirPath())).filePath(QString::fromUtf8("音频文件"));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_videoManager->handleEventFilter(watched, event, this)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
