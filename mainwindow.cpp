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
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDateTime>

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

    m_view1Button = new QPushButton(tr("1路"), this);
    m_view4Button = new QPushButton(tr("4路"), this);
    m_view8Button = new QPushButton(tr("8路"), this);
    m_view1Button->setCheckable(true);
    m_view4Button->setCheckable(true);
    m_view8Button->setCheckable(true);
    m_view4Button->setChecked(true);
    auto *viewButtonGroup = new QWidget(this);
    auto *viewLayout = new QHBoxLayout(viewButtonGroup);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->addWidget(new QLabel(tr("视图:"), this));
    viewLayout->addWidget(m_view1Button);
    viewLayout->addWidget(m_view4Button);
    viewLayout->addWidget(m_view8Button);

    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);

    m_timeLabel = new QLabel(CanDataManager::formatTime(0) + " / " + CanDataManager::formatTime(0), this);
    m_timeLabel->setMinimumWidth(150);

    m_timestampLabel = new QLabel(tr("时间戳: --"), this);
    m_timestampLabel->setMinimumWidth(350);

    m_canUpdateTimer = new QTimer(this);
    m_canUpdateTimer->setInterval(500);

    auto *centerLayout = new QVBoxLayout;
    centerLayout->setSpacing(4);
    centerLayout->addWidget(videoGridWidget, 1);
    centerLayout->addWidget(m_positionSlider);

    auto *infoLayout = new QHBoxLayout;
    infoLayout->addWidget(m_timestampLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_timeLabel);
    centerLayout->addLayout(infoLayout);

    auto *playControlLayout = new QHBoxLayout;
    playControlLayout->addWidget(m_playButton);
    playControlLayout->addWidget(new QLabel(tr("倍速:"), this));
    playControlLayout->addWidget(m_speedComboBox);
    playControlLayout->addWidget(viewButtonGroup);
    playControlLayout->addStretch();
    centerLayout->addLayout(playControlLayout);

    auto *centerWidget = new QWidget(this);
    centerWidget->setLayout(centerLayout);

    // ===== 右侧CAN数据面板 =====
    m_canStatusLabel = new QLabel(tr("未加载 CAN Log"), this);

    m_canTable = new QTableView(this);
    m_canTableModel = new SimpleTableModel({tr("信号名称"), tr("值(信号来源)"), tr("单位")}, this);
    m_canTable->setModel(m_canTableModel);
    m_canTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canTable->horizontalHeader()->setStretchLastSection(true);
    m_canTable->setColumnWidth(0, 200);
    m_canTable->setColumnWidth(1, 120);
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canTable->setAlternatingRowColors(true);

    m_canRawTable = new QTableView(this);
    m_canRawTableModel = new SimpleTableModel({tr("CAN ID"), tr("类型"), tr("DLC"), tr("数据")}, this);
    m_canRawTable->setModel(m_canRawTableModel);
    m_canRawTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canRawTable->horizontalHeader()->setStretchLastSection(true);
    m_canRawTable->setColumnWidth(0, 120);
    m_canRawTable->setColumnWidth(1, 60);
    m_canRawTable->setColumnWidth(2, 50);
    m_canRawTable->verticalHeader()->setVisible(false);
    m_canRawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canRawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canRawTable->setAlternatingRowColors(true);

    // 注入CAN表格到CanDataManager
    m_canManager->setCanTables(m_canTable, m_canRawTable, m_canTableModel, m_canRawTableModel);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_canStatusLabel);
    rightLayout->addWidget(m_canTable, 1);
    rightLayout->addWidget(m_canRawTable, 1);

    auto *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);
    rightWidget->setMinimumWidth(250);

    // ===== 主分割器: 左 | 中 | 右 =====
    auto *splitter = new QSplitter(this);
    splitter->addWidget(leftWidget);
    splitter->addWidget(centerWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);   // 左侧: 固定宽度
    splitter->setStretchFactor(1, 5);   // 中间: 视频区域(更大)
    splitter->setStretchFactor(2, 2);   // 右侧: CAN数据区域
    QList<int> sizes;
    sizes << 260 << 1000 << 400;
    splitter->setSizes(sizes);

    setCentralWidget(splitter);
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
            m_baseTimestampMs = timestamp;
            m_canManager->setBaseTimestampMs(timestamp);
            m_videoManager->playAllChannels(timestamp);
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

    // 视图切换按钮
    connect(m_view1Button, &QPushButton::clicked, this, [this]() {
        m_videoManager->setViewMode(VideoPlayerManager::View1);
    });
    connect(m_view4Button, &QPushButton::clicked, this, [this]() {
        m_videoManager->setViewMode(VideoPlayerManager::View4);
    });
    connect(m_view8Button, &QPushButton::clicked, this, [this]() {
        m_videoManager->setViewMode(VideoPlayerManager::View8);
    });

    // 视图模式变化时更新按钮
    connect(m_videoManager, &VideoPlayerManager::viewModeChanged, this, [this](VideoPlayerManager::ViewMode mode) {
        m_view1Button->setChecked(mode == VideoPlayerManager::View1);
        m_view4Button->setChecked(mode == VideoPlayerManager::View4);
        m_view8Button->setChecked(mode == VideoPlayerManager::View8);
    });

    // 为每个视频widget添加事件过滤器
    for (int i = 0; i < VideoPlayerManager::MAX_CHANNELS; i++) {
        m_videoManager->channelContainer(i)->installEventFilter(this);
    }

    // 主播放器(通道0)驱动slider和CAN更新
    QMediaPlayer *masterPlayer = m_videoManager->masterPlayer();
    connect(masterPlayer, &QMediaPlayer::positionChanged, this, &MainWindow::updatePosition);
    connect(masterPlayer, &QMediaPlayer::durationChanged, this, &MainWindow::updateDuration);

    // 监听主播放器状态变化
    connect(masterPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });

    // 倍速选择器
    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        qreal rate = m_speedComboBox->itemData(index).toDouble();
        m_videoManager->setPlaybackRate(rate);
        setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
    });

    // CAN更新定时器
    connect(m_canUpdateTimer, &QTimer::timeout, this, [this]() {
        if (m_videoManager->masterPlayer()->playbackState() != QMediaPlayer::PlayingState) {
            qint64 displayPos = (m_canManager->lastSeekPositionMs() >= 0)
                ? m_canManager->lastSeekPositionMs()
                : m_videoManager->masterPlayer()->position();
            m_canManager->updateCanDisplay(displayPos);
        }
    });
    m_canUpdateTimer->start();

    // 进度条拖动
    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() {
        m_sliderPressed = true;
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
        m_sliderPressed = false;
        qint64 newPosition = m_positionSlider->value();
        m_videoManager->seekAllPlayers(newPosition);

        qint64 newAbsoluteTimestamp = m_baseTimestampMs + newPosition;

        bool inRange = false;
        if (m_canManager->hasCanData()) {
            qint64 firstAbsolute = m_baseTimestampMs + m_canManager->firstFrameTimeMs();
            qint64 lastAbsolute = m_baseTimestampMs + m_canManager->lastFrameTimeMs();
            inRange = (newAbsoluteTimestamp >= firstAbsolute && newAbsoluteTimestamp <= lastAbsolute);
        }

        if (!inRange) {
            m_canManager->loadCanLogByTimestamp(m_baseTimestampMs, newPosition);
        } else {
            m_canManager->updateCanDisplay(newPosition);
        }

        m_canManager->setLastSeekPositionMs(newPosition);
        m_canManager->setLastCanUpdateMs(-1);
        m_seekDebounceTimer->start();
    });
    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_sliderPressed) {
            if (m_baseTimestampMs > 0) {
                qint64 startTimestamp = m_baseTimestampMs;
                qint64 endTimestamp = m_baseTimestampMs + m_durationMs;
                qint64 currentTimestamp = m_baseTimestampMs + value;
                qint64 remainingMs = endTimestamp - currentTimestamp;
                m_timeLabel->setText(tr("开始: %1 | 结束: %2 | 剩余: %3 ms")
                    .arg(startTimestamp)
                    .arg(endTimestamp)
                    .arg(remainingMs));
            } else {
                m_timeLabel->setText(CanDataManager::formatTime(value) + " / " + CanDataManager::formatTime(m_durationMs));
            }

            if (m_baseTimestampMs > 0) {
                qint64 currentTimestamp = m_baseTimestampMs + value;
                m_timestampLabel->setText(CanDataManager::formatDateTime(currentTimestamp) + " | " + tr("时间戳: %1").arg(currentTimestamp));
            } else {
                m_timestampLabel->setText(tr("时间戳: 偏移 %1 ms").arg(value));
            }
        }
    });

    // 播放按钮
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);

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

    bool isPlaying = (m_videoManager->masterPlayer()->playbackState() == QMediaPlayer::PlayingState);

    if (isPlaying) {
        m_videoManager->togglePlayback();
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
        m_videoManager->togglePlayback();
    }
}

void MainWindow::setPlaybackRate(qreal rate)
{
    m_videoManager->setPlaybackRate(rate);
    setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
}

void MainWindow::seekFromSlider(int value)
{
    m_videoManager->seekAllPlayers(value);
}

// ===== 位置更新 =====

void MainWindow::updatePosition(qint64 positionMs)
{
    if (!m_sliderPressed) {
        m_positionSlider->setValue(static_cast<int>(positionMs));
    }

    // 时间戳显示
    if (m_baseTimestampMs > 0) {
        qint64 startTimestamp = m_baseTimestampMs;
        qint64 endTimestamp = m_baseTimestampMs + m_durationMs;
        qint64 currentTimestamp = m_baseTimestampMs + positionMs;
        qint64 remainingMs = endTimestamp - currentTimestamp;
        m_timeLabel->setText(tr("开始: %1 | 结束: %2 | 剩余: %3 ms")
            .arg(startTimestamp)
            .arg(endTimestamp)
            .arg(remainingMs));
    } else {
        m_timeLabel->setText(CanDataManager::formatTime(positionMs) + " / " + CanDataManager::formatTime(m_durationMs));
    }

    // 更新时间戳绝对值显示
    if (m_baseTimestampMs > 0) {
        qint64 currentTimestamp = m_baseTimestampMs + positionMs;
        m_timestampLabel->setText(CanDataManager::formatDateTime(currentTimestamp) + " | " + tr("时间戳: %1").arg(currentTimestamp));

        qint64 checkTimestamp = (m_canManager->lastSeekPositionMs() >= 0)
            ? (m_baseTimestampMs + m_canManager->lastSeekPositionMs())
            : currentTimestamp;
        m_canManager->loadNextCanLogIfNeeded(checkTimestamp);
    } else {
        m_timestampLabel->setText(tr("时间戳: 偏移 %1 ms").arg(positionMs));
    }

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
}

void MainWindow::updateDuration(qint64 durationMs)
{
    m_durationMs = durationMs;
    m_positionSlider->setRange(0, static_cast<int>(std::min<qint64>(durationMs, std::numeric_limits<int>::max())));

    if (m_baseTimestampMs > 0) {
        qint64 startTimestamp = m_baseTimestampMs;
        qint64 endTimestamp = m_baseTimestampMs + durationMs;
        qint64 remainingMs = durationMs;
        m_timeLabel->setText(tr("开始: %1 | 结束: %2 | 剩余: %3 ms")
            .arg(startTimestamp)
            .arg(endTimestamp)
            .arg(remainingMs));
    } else {
        m_timeLabel->setText(CanDataManager::formatTime(m_videoManager->masterPlayer()->position()) + " / " + CanDataManager::formatTime(durationMs));
    }
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

// ===== 文件夹选择 =====

void MainWindow::openVideoFolder()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择视频文件夹"), "../..");
    if (folderPath.isEmpty()) return;

    m_videoManager->setVideoFolderPath(folderPath);
    m_videoFolderLabel->setText(folderPath);
    m_videoFolderLabel->setStyleSheet("QLabel { color: black; font-size: 11px; }");
    setStatus(tr("已选择视频文件夹: %1").arg(folderPath));

    loadVideoList(m_currentDate);
}

void MainWindow::openCanLogFolder()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择CAN Log文件夹"), "../..");
    if (folderPath.isEmpty()) return;

    m_canManager->setCanLogFolderPath(folderPath);
    m_canManager->loadCanLogFolder();
    setStatus(tr("已选择CAN Log文件夹: %1").arg(folderPath));

    // 如果视频正在播放或已有基准时间戳，自动加载对应的CAN数据
    if (m_baseTimestampMs > 0 && !m_canManager->canLogFileList().isEmpty()) {
        qint64 currentPosition = m_videoManager->masterPlayer()->position();
        m_canManager->loadCanLogByTimestamp(m_baseTimestampMs, currentPosition);
    }
}

void MainWindow::onDateChanged(const QDate &date)
{
    if (!m_videoManager->videoFolderPath().isEmpty()) {
        m_videoManager->loadAllChannels(date);
    }
}

void MainWindow::loadVideoList(const QDate &date)
{
    m_videoManager->loadAllChannels(date);
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

// ===== 事件过滤 =====

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (m_videoManager->handleEventFilter(watched, event, this)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}
