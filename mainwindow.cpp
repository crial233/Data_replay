#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "simple_table_model.h"
#include "can_data_manager.h"
#include "video_player_manager.h"

#include <QCalendarWidget>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDateTime>
#include <QDialog>

#include <algorithm>
#include <limits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 创建子模块
    m_videoManager = new VideoPlayerManager(this);
    m_canManager = new CanDataManager(this);

    buildUi();
    initializeDefaultFolders();
    connectSignals();

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

    // ===== 创建8路播放器+视频widget =====
    m_videoManager->createPlayers(this);

    // ===== 视频网格布局 =====
    auto *videoGridWidget = new QWidget(this);
    videoGridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoManager->setVideoGridWidget(videoGridWidget);

    // ===== 日历widget(常时展开) =====
    m_calendarWidget = new QCalendarWidget(this);
    m_calendarWidget->setSelectedDate(QDate::currentDate());
    m_calendarWidget->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
    m_calendarWidget->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    m_calendarWidget->setGridVisible(true);
    m_currentDate = QDate::currentDate();

    // ===== 左侧面板: 文件夹选择 + 视频列表 + 日历 =====
    m_openVideoFolderButton = new QPushButton(tr("选择视频文件夹"), this);
    m_openCanLogFolderButton = new QPushButton(tr("选择CAN Log文件夹"), this);
    m_videoFolderLabel = new QLabel(tr("未选择视频文件夹"), this);
    m_videoFolderLabel->setWordWrap(true);
    m_videoFolderLabel->setStyleSheet("QLabel { color: gray; font-size: 11px; }");

    m_videoListWidget = new QListWidget(this);
    m_videoListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_videoManager->setVideoListWidget(m_videoListWidget);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->setSpacing(4);
    leftLayout->addWidget(m_openVideoFolderButton);
    leftLayout->addWidget(m_openCanLogFolderButton);
    leftLayout->addWidget(m_videoFolderLabel);
    leftLayout->addWidget(new QLabel(tr("视频列表:"), this));
    leftLayout->addWidget(m_videoListWidget, 1);
    leftLayout->addWidget(m_calendarWidget);

    auto *leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);
    leftWidget->setMinimumWidth(220);
    leftWidget->setMaximumWidth(320);

    // ===== 中间面板: 视频播放区 + 控制栏 =====
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

    // 注入CAN表格到CanDataManager
    m_canManager->setCanTables(m_canTable, m_canRawTable, m_canTableModel, m_canRawTableModel);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_canStatusLabel);
    rightLayout->addWidget(m_showRawCanFramesButton);
    rightLayout->addWidget(m_canTable, 1);
    rightLayout->addWidget(m_canRawTable, 1);

    m_canSidebarWidget = new QWidget(this);
    m_canSidebarWidget->setLayout(rightLayout);
    m_canSidebarWidget->setMinimumWidth(250);

    m_canSidebarEdgeButton = new QPushButton(tr(">"), this);
    m_canSidebarEdgeButton->setFixedSize(24, 72);
    m_canSidebarEdgeButton->setCursor(Qt::PointingHandCursor);
    m_canSidebarEdgeButton->setToolTip(tr("显示CAN数据"));
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

    // ===== 主分割器: 左 | 中 | CAN | 右侧按钮 =====
    m_mainSplitter = new QSplitter(this);
    m_mainSplitter->addWidget(leftWidget);
    m_mainSplitter->addWidget(centerWidget);
    m_mainSplitter->addWidget(m_canSidebarWidget);
    m_mainSplitter->addWidget(m_canSidebarToggleStrip);
    m_mainSplitter->setStretchFactor(0, 0);   // 左侧: 固定宽度
    m_mainSplitter->setStretchFactor(1, 5);   // 中间: 视频区域(更大)
    m_mainSplitter->setStretchFactor(2, 2);   // 右侧: CAN数据区域
    m_mainSplitter->setStretchFactor(3, 0);   // 右侧按钮: 固定宽度
    QList<int> sizes;
    sizes << 260 << 1400 << 0 << 28;
    m_mainSplitter->setSizes(sizes);
    m_canSidebarWidget->setVisible(false);

    setCentralWidget(m_mainSplitter);
    setStatus(tr("请选择视频文件夹和 CAN Log文件夹，然后选择日期加载视频列表"));
}

void MainWindow::connectSignals()
{
    // 文件夹选择按钮
    connect(m_openVideoFolderButton, &QPushButton::clicked, this, &MainWindow::openVideoFolder);
    connect(m_openCanLogFolderButton, &QPushButton::clicked, this, &MainWindow::openCanLogFolder);

    // 视频列表选择
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
            m_timelineWidget->setPositionMs(dayOffsetMs);
            setPlaybackUi(true);
        }
    });

    // 日历选择
    connect(m_calendarWidget, &QCalendarWidget::selectionChanged, this, [this]() {
        QDate newDate = m_calendarWidget->selectedDate();
        if (newDate != m_currentDate) {
            m_currentDate = newDate;
            onDateChanged(newDate);
        }
    });

    // 为每个视频widget添加事件过滤器
    for (int i = 0; i < VideoPlayerManager::MAX_CHANNELS; i++) {
        m_videoManager->channelContainer(i)->installEventFilter(this);
        m_videoManager->videoWidget(i)->installEventFilter(this);
    }

    // 主播放器只提供媒体状态，回放进度由 ReplayClock 统一驱动
    QMediaPlayer *masterPlayer = m_videoManager->masterPlayer();
    connect(masterPlayer, &QMediaPlayer::durationChanged, this, &MainWindow::updateDuration);

    // 监听主播放器状态变化
    connect(masterPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });

    // 倍速选择器
    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        qreal rate = m_speedComboBox->itemData(index).toDouble();
        m_replayClock.setPlaybackRate(rate);
        m_videoManager->setPlaybackRate(rate);
        setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
    });

    // CAN更新定时器
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

    // 播放按钮
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
        if (!m_canSidebarWidget || !m_mainSplitter) {
            return;
        }

        const bool showSidebar = !m_canSidebarWidget->isVisible();
        m_canSidebarWidget->setVisible(showSidebar);
        m_canSidebarEdgeButton->setText(showSidebar ? tr("<") : tr(">"));
        m_canSidebarEdgeButton->setToolTip(showSidebar ? tr("隐藏CAN数据") : tr("显示CAN数据"));

        if (showSidebar) {
            QList<int> sizes = m_mainSplitter->sizes();
            if (sizes.size() == 4 && sizes[2] <= 0) {
                sizes[2] = 400;
                if (sizes[1] > 400) {
                    sizes[1] -= 400;
                }
                sizes[3] = 28;
                m_mainSplitter->setSizes(sizes);
            }
        }
    });

    // VideoPlayerManager信号
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

    // CanDataManager信号
    connect(m_canManager, &CanDataManager::statusMessage, this, [this](const QString &msg) {
        m_canStatusLabel->setText(msg);
    });
}

// ===== 播放控制 =====

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
        updateFromReplayClock();
    } else {
        // 播放前检查是否有视频
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
    }
    setPlaybackUi(m_replayClock.isPlaying());
}

void MainWindow::setPlaybackRate(qreal rate)
{
    m_replayClock.setPlaybackRate(rate);
    m_videoManager->setPlaybackRate(rate);
    setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
}

void MainWindow::seekOnTimeline(qint64 positionMs)
{
    m_replayClock.seek(positionMs);
    m_videoManager->syncPlayersToTimestamp(m_replayClock.currentTimestampMs(), 0, m_replayClock.isPlaying());

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

// ===== 位置更新 =====

void MainWindow::updateFromReplayClock()
{
    qint64 offsetMs = m_replayClock.currentOffsetMs();
    if (m_durationMs > 0 && offsetMs > m_durationMs) {
        offsetMs = m_durationMs;
        m_replayClock.seek(offsetMs);
        m_replayClock.pause();
        m_videoManager->pauseAll();
        setPlaybackUi(false);
    }

    m_videoManager->syncPlayersToTimestamp(
        m_replayClock.currentTimestampMs(),
        m_replayClock.isPlaying() ? 150 : 0,
        m_replayClock.isPlaying());

    updatePosition(offsetMs);
}

void MainWindow::updatePosition(qint64 positionMs)
{
    m_timelineWidget->setPositionMs(positionMs);

    // 时间戳显示
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

    // 降低CAN显示更新频率
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
    updateFromReplayClock();
    setPlaybackUi(false);
    m_playButton->setEnabled(false);

    const qint64 positionMs = (m_canManager->lastSeekPositionMs() >= 0)
        ? m_canManager->lastSeekPositionMs()
        : m_replayClock.currentOffsetMs();

    auto *dialog = new QDialog(this);
    m_rawCanFramesDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("当前时间戳前后 CAN 原始帧"));
    dialog->resize(1350, 720);
    connect(dialog, &QObject::destroyed, this, [this]() {
        m_playButton->setEnabled(true);
        m_rawCanFramesDialog = nullptr;
        m_rawCanFramesInfoLabel = nullptr;
        m_rawCanFramesTable = nullptr;
        m_rawCanFramesModel = nullptr;
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

    m_rawCanFramesTable = new QTableView(dialog);
    m_rawCanFramesModel = new SimpleTableModel({tr("解析时间戳"), tr("原始时间戳"), tr("CAN ID"), tr("数据"), tr("解析结果")}, dialog);
    m_rawCanFramesTable->setModel(m_rawCanFramesModel);
    m_rawCanFramesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_rawCanFramesTable->horizontalHeader()->setStretchLastSection(false);
    m_rawCanFramesTable->setColumnWidth(0, 180);
    m_rawCanFramesTable->setColumnWidth(1, 160);
    m_rawCanFramesTable->setColumnWidth(2, 90);
    m_rawCanFramesTable->setColumnWidth(3, 240);
    m_rawCanFramesTable->setColumnWidth(4, 650);
    m_rawCanFramesTable->verticalHeader()->setVisible(false);
    m_rawCanFramesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rawCanFramesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rawCanFramesTable->setAlternatingRowColors(true);
    layout->addWidget(m_rawCanFramesTable, 1);

    connect(m_rawCanFramesTable, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (!index.isValid() || !m_rawCanFramesModel) {
            return;
        }
        const QString parsedDateTime = m_rawCanFramesModel->data(m_rawCanFramesModel->index(index.row(), 0)).toString();
        const QString parsedTime = parsedDateTime.section('|', 1).trimmed();
        const QTime parsed = QTime::fromString(parsedTime, "HH:mm:ss.zzz");
        if (!parsed.isValid()) {
            return;
        }

        seekOnTimeline(QTime(0, 0).msecsTo(parsed));
        m_replayClock.pause();
        m_videoManager->pauseAll();
        setPlaybackUi(false);
    });

    refreshRawCanFramesDialog(positionMs);
    dialog->show();
}

void MainWindow::refreshRawCanFramesDialog(qint64 positionMs)
{
    if (!m_rawCanFramesDialog || !m_rawCanFramesModel || !m_rawCanFramesInfoLabel) {
        return;
    }

    QVector<QStringList> rows = m_canManager->rawFrameRowsAround(positionMs, 500);
    QString currentOriginalTimestamp = tr("无");
    qint64 bestDiff = -1;
    for (const QStringList &row : rows) {
        if (row.size() < 2) {
            continue;
        }
        const QTime parsed = QTime::fromString(row[0].section('|', 1).trimmed(), "HH:mm:ss.zzz");
        if (!parsed.isValid()) {
            continue;
        }
        const qint64 rowTime = QTime(0, 0).msecsTo(parsed);
        const qint64 diff = qAbs(rowTime - positionMs);
        if (bestDiff < 0 || diff < bestDiff) {
            bestDiff = diff;
            currentOriginalTimestamp = row[1];
        }
    }

    m_rawCanFramesModel->setRows(std::move(rows));
    m_rawCanFramesInfoLabel->setText(tr("解析时间戳: %1 | 原始时间戳: %2 | 显示前后500ms内原始帧%3")
        .arg(CanDataManager::formatDateTime(m_baseTimestampMs + positionMs))
        .arg(currentOriginalTimestamp)
        .arg(m_rawCanFramesModel->rowCount() == 0 ? tr(" | 无 CAN 原始帧") : QString()));
}
// ===== UI辅助 =====

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

    if (QDir(videoPath).exists() || QDir(canLogPath).exists()) {
        setStatus(tr("已加载默认路径：视频=%1 | CAN Log=%2").arg(videoPath, canLogPath));
    }
}

// ===== 文件夹选择 =====

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

    // 如果视频正在播放或已有基准时间戳，自动加载对应的CAN数据
    if (m_baseTimestampMs > 0 && !m_canManager->canLogFileList().isEmpty()) {
        qint64 currentPosition = m_replayClock.currentOffsetMs();
        m_canManager->loadCanLogByTimestamp(m_baseTimestampMs, currentPosition);
    }
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
    setPlaybackUi(false);

    if (!m_videoManager->videoFolderPath().isEmpty()) {
        loadVideoList(date);
    }
}

void MainWindow::loadVideoList(const QDate &date)
{
    m_videoManager->loadAllChannels(date);
    m_timelineWidget->setAvailabilityRanges(m_videoManager->availabilityRangesForDate(date));
}

// ===== 时间戳工具 =====

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
    return QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath(QString::fromUtf8("视频文件"));
}

QString MainWindow::defaultCanLogFolderPath() const
{
    return QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath(QString::fromUtf8("log文件"));
}

// ===== 事件过滤 =====

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_videoManager->handleEventFilter(watched, event, this)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
