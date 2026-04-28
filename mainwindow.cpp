#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QAudioOutput>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTextEdit>
#include <QTextStream>
#include <QTime>
#include <QDateTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QRegularExpression>

#include <algorithm>
#include <limits>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    buildUi();
    connectSignals();
    
    // 初始化拖动防抖定时器（500ms）
    m_seekDebounceTimer = new QTimer(this);
    m_seekDebounceTimer->setSingleShot(true);
    m_seekDebounceTimer->setInterval(500);
    connect(m_seekDebounceTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[seekDebounceTimer] 超时，清除m_lastSeekPositionMs，当前值=" << m_lastSeekPositionMs;
        m_lastSeekPositionMs = -1;  // 清除拖动标记
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("Data Replay"));
    resize(1400, 800);  // 增大默认窗口尺寸
    setMinimumSize(1200, 700);  // 设置最小窗口尺寸

    m_player = new QMediaPlayer(this);
#if QT_VERSION_MAJOR >= 6
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
#endif

    m_videoWidget = new QVideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    m_openVideoFolderButton = new QPushButton(tr("选择视频文件夹"), this);
    m_openCanLogFolderButton = new QPushButton(tr("选择CAN Log文件夹"), this);
    m_playButton = new QPushButton(tr("播放"), this);

    // 倍速选择器
    m_speedComboBox = new QComboBox(this);
    m_speedComboBox->addItem("x0.01", 0.01);
    m_speedComboBox->addItem("x0.05", 0.05);
    m_speedComboBox->addItem("x0.1", 0.1);
    m_speedComboBox->addItem("x0.25", 0.25);
    m_speedComboBox->addItem("x0.5", 0.5);
    m_speedComboBox->addItem("x1.0", 1.0);
    m_speedComboBox->addItem("x1.5", 1.5);
    m_speedComboBox->addItem("x2.0", 2.0);
    m_speedComboBox->setCurrentIndex(5);  // 默认x1.0
    m_speedComboBox->setMaximumWidth(80);  // 限制下拉框宽度

    // 日期选择按钮
    m_dateButton = new QPushButton(QDate::currentDate().toString("yyyy-MM-dd"), this);
    m_currentDate = QDate::currentDate();  // 记录当前选择的日期
        
    m_positionSlider = new QSlider(Qt::Horizontal, this);
    m_positionSlider->setRange(0, 0);

    m_timeLabel = new QLabel(formatTime(0) + " / " + formatTime(0), this);
    m_timeLabel->setMinimumWidth(150);

    m_timestampLabel = new QLabel(tr("时间戳: --"), this);
    m_timestampLabel->setMinimumWidth(350);

    // 视频列表
    m_videoListWidget = new QListWidget(this);
    m_videoListWidget->setMinimumHeight(100);
    m_videoListWidget->setMaximumHeight(150);
    m_videoListWidget->hide();  // 初始隐藏
    
    // CAN显示更新定时器（用于暂停时也能显示当前帧的CAN数据）
    m_canUpdateTimer = new QTimer(this);
    m_canUpdateTimer->setInterval(500);  // 500ms更新一次

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_openVideoFolderButton);
    buttonLayout->addWidget(m_openCanLogFolderButton);
    buttonLayout->addWidget(m_dateButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_timestampLabel);
    buttonLayout->addWidget(m_timeLabel);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->addWidget(m_videoWidget, 1);
    leftLayout->addWidget(m_videoListWidget);  // 视频列表（在视频下方）
    leftLayout->addWidget(m_positionSlider);
    leftLayout->addLayout(buttonLayout);
    
    // 添加播放控制栏（放在最底部）
    auto *playControlLayout = new QHBoxLayout;
    playControlLayout->addWidget(m_playButton);
    playControlLayout->addWidget(new QLabel(tr("倍速播放:"), this));
    playControlLayout->addWidget(m_speedComboBox);
    playControlLayout->addStretch();
    leftLayout->addLayout(playControlLayout);

    auto *leftWidget = new QWidget(this);
    leftWidget->setLayout(leftLayout);

    m_canStatusLabel = new QLabel(tr("未加载 CAN Log"), this);
    
    // 上面的表格：信号解析（中文名称+实际数值）- 高性能QTableView+Model
    m_canTable = new QTableView(this);
    m_canTableModel = new SimpleTableModel({tr("信号名称"), tr("值(信号来源)"), tr("单位")}, this);
    m_canTable->setModel(m_canTableModel);
    m_canTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canTable->horizontalHeader()->setStretchLastSection(true);
    m_canTable->setColumnWidth(0, 200);  // 信号名称
    m_canTable->setColumnWidth(1, 120);  // 值
    m_canTable->verticalHeader()->setVisible(false);
    m_canTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canTable->setAlternatingRowColors(true);

    // 下面的表格：PCAN-View风格（CAN ID + 数据）- 高性能QTableView+Model
    m_canRawTable = new QTableView(this);
    m_canRawTableModel = new SimpleTableModel({tr("CAN ID"), tr("类型"), tr("DLC"), tr("数据")}, this);
    m_canRawTable->setModel(m_canRawTableModel);
    m_canRawTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_canRawTable->horizontalHeader()->setStretchLastSection(true);
    m_canRawTable->setColumnWidth(0, 120);  // CAN ID
    m_canRawTable->setColumnWidth(1, 60);   // 类型
    m_canRawTable->setColumnWidth(2, 50);   // DLC
    m_canRawTable->verticalHeader()->setVisible(false);
    m_canRawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_canRawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_canRawTable->setAlternatingRowColors(true);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->addWidget(m_canStatusLabel);
    rightLayout->addWidget(m_canTable, 1);  // 上面：信号解析表格
    rightLayout->addWidget(m_canRawTable, 1);  // 下面：PCAN-View风格表格

    auto *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);

    auto *splitter = new QSplitter(this);
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);  // 左侧视频区域
    splitter->setStretchFactor(1, 3);  // 右侧CAN数据区域（更大）

    setCentralWidget(splitter);
    setStatus(tr("请选择视频文件夹和 CAN Log文件夹，然后选择日期加载视频列表"));
}

void MainWindow::connectSignals()
{
    connect(m_openVideoFolderButton, &QPushButton::clicked, this, &MainWindow::openVideoFolder);
    connect(m_openCanLogFolderButton, &QPushButton::clicked, this, &MainWindow::openCanLogFolder);
    
    // 日期按钮点击弹出日历对话框
    connect(m_dateButton, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("选择日期"));
        dialog.setMinimumSize(300, 250);
        
        QVBoxLayout layout(&dialog);
        
        QCalendarWidget *calendar = new QCalendarWidget(&dialog);
        calendar->setSelectedDate(m_currentDate);
        layout.addWidget(calendar);
        
        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout.addWidget(buttonBox);
        
        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        
        if (dialog.exec() == QDialog::Accepted) {
            QDate newDate = calendar->selectedDate();
            if (newDate != m_currentDate) {
                m_currentDate = newDate;
                m_dateButton->setText(newDate.toString("yyyy-MM-dd"));
                onDateChanged(newDate);
            }
        }
    });
    
    connect(m_videoListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onVideoSelected);
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MainWindow::updatePosition);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MainWindow::updateDuration);
    
    // 监听播放器状态变化，自动更新按钮文本
#if QT_VERSION_MAJOR >= 6
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });
#else
    connect(m_player, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });
#endif
    
    // 倍速选择器信号
    connect(m_speedComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        qreal rate = m_speedComboBox->itemData(index).toDouble();
        setPlaybackRate(rate);
    });
    
    // CAN更新定时器：暂停时也要更新显示
    connect(m_canUpdateTimer, &QTimer::timeout, this, [this]() {
#if QT_VERSION_MAJOR >= 6
        if (m_player->playbackState() != QMediaPlayer::PlayingState) {
#else
        if (m_player->state() != QMediaPlayer::PlayingState) {
#endif
            // 只在暂停时更新
            // 如果刚刚拖动过，使用拖动位置而非播放器位置
            qint64 displayPos = (m_lastSeekPositionMs >= 0) ? m_lastSeekPositionMs : m_player->position();
            qDebug() << "[canUpdateTimer] m_lastSeekPositionMs=" << m_lastSeekPositionMs 
                     << "m_player->position()=" << m_player->position() 
                     << "displayPos=" << displayPos;
            updateCanDisplay(displayPos);
        }
    });
    m_canUpdateTimer->start();

    connect(m_positionSlider, &QSlider::sliderPressed, this, [this]() {
        m_sliderPressed = true;
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]() {
        m_sliderPressed = false;
        // 拖动结束后seek
        m_player->setPosition(m_positionSlider->value());
        
        qint64 newPosition = m_positionSlider->value();
        qint64 newAbsoluteTimestamp = m_baseTimestampMs + newPosition;
        
        // 检查新位置是否在已加载数据范围内
        bool inRange = false;
        if (!m_canFrames.isEmpty()) {
            qint64 firstAbsolute = m_baseTimestampMs + m_canFrames.first().timeMs;
            qint64 lastAbsolute = m_baseTimestampMs + m_canFrames.last().timeMs;
            inRange = (newAbsoluteTimestamp >= firstAbsolute && newAbsoluteTimestamp <= lastAbsolute);
            qDebug() << "[sliderReleased] 拖动到位置:" << newPosition 
                     << "数据范围:" << m_canFrames.first().timeMs << "~" << m_canFrames.last().timeMs
                     << "inRange:" << inRange << "帧数:" << m_canFrames.size();
        } else {
            qDebug() << "[sliderReleased] m_canFrames为空";
        }
        
        if (!inRange) {
            // 拖动到未加载区域，重新加载数据
            qDebug() << "[sliderReleased] 拖动到未加载区域，重新加载:" << newAbsoluteTimestamp;
            loadCanLogByTimestamp(m_baseTimestampMs, newPosition);
        } else {
            // 在已加载范围内，立即更新显示
            qDebug() << "[sliderReleased] 在已加载范围内，直接更新显示";
            updateCanDisplay(newPosition);
        }
        
        // 记录拖动位置，在500ms内优先使用此位置而非播放器位置
        m_lastSeekPositionMs = newPosition;
        m_lastCanUpdateMs = -1;  // 重置CAN更新频率限制，确保拖动后立即刷新
        m_seekDebounceTimer->start();
        qDebug() << "[sliderReleased] 设置m_lastSeekPositionMs=" << newPosition << "启动防抖定时器";
    });
    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int value) {
        // 拖动时只更新时间标签，不更新CAN显示（避免卡顿）
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
                m_timeLabel->setText(formatTime(value) + " / " + formatTime(m_durationMs));
            }
            
            // 更新时间戳绝对值显示和对应的日期时间
            if (m_baseTimestampMs > 0) {
                qint64 currentTimestamp = m_baseTimestampMs + value;
                m_timestampLabel->setText(formatDateTime(currentTimestamp) + " | " + tr("时间戳: %1").arg(currentTimestamp));
            } else {
                m_timestampLabel->setText(tr("时间戳: 偏移 %1 ms").arg(value));
            }
        }
    });

#if QT_VERSION_MAJOR >= 6
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });
#else
    connect(m_player, &QMediaPlayer::stateChanged, this, [this](QMediaPlayer::State state) {
        setPlaybackUi(state == QMediaPlayer::PlayingState);
    });
#endif
}

void MainWindow::togglePlayback()
{
    // 播放前检查必要路径是否已选择
    if (m_videoFolderPath.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择视频文件夹"));
        return;
    }
    if (m_canLogFolderPath.isEmpty()) {
        QMessageBox::warning(this, tr("提示"), tr("请先选择CAN Log文件夹"));
        return;
    }
    
    // 如果没有选中视频，自动选中列表第一个并播放
    if (m_videoListWidget->currentRow() < 0) {
        if (!m_videoFileList.isEmpty()) {
            m_videoListWidget->setCurrentRow(0);
            // onVideoSelected会自动加载并播放视频
            return;
        } else {
            QMessageBox::warning(this, tr("提示"), tr("当前日期没有视频文件"));
            return;
        }
    }
    
#if QT_VERSION_MAJOR >= 6
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        // 从停止/暂停状态开始播放，重置CAN更新限制以确保立即刷新
        if (m_player->playbackState() == QMediaPlayer::StoppedState) {
            m_lastCanUpdateMs = -1;
        }
        m_player->play();
    }
#else
    if (m_player->state() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        // 从停止/暂停状态开始播放，重置CAN更新限制以确保立即刷新
        if (m_player->state() == QMediaPlayer::StoppedState) {
            m_lastCanUpdateMs = -1;
        }
        m_player->play();
    }
#endif
}

void MainWindow::setPlaybackRate(qreal rate)
{
#if QT_VERSION_MAJOR >= 6
    m_player->setPlaybackRate(rate);
#else
    // Qt5 不支持动态修改播放速率，需要提示用户
    Q_UNUSED(rate);
    setStatus(tr("Qt5 不支持动态修改播放速率"));
#endif
    setStatus(tr("播放速率: x%1").arg(rate, 0, 'f', 2));
}

void MainWindow::seekFromSlider(int value)
{
    m_player->setPosition(value);
    
    // 拖动时不更新CAN表格（避免卡顿），只更新时间标签（由valueChanged处理）
    // updateCanDisplay(value);  // ❌ 注释掉，拖动时不更新
    
    // 拖动后，由sliderReleased触发延迟更新，不需要在这里处理加载逻辑
}

void MainWindow::updatePosition(qint64 positionMs)
{
    if (!m_sliderPressed) {
        m_positionSlider->setValue(static_cast<int>(positionMs));
    }
    
    // 使用时间戳显示视频进度和剩余时间
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
        m_timeLabel->setText(formatTime(positionMs) + " / " + formatTime(m_durationMs));
    }
    
    // 更新时间戳绝对值显示和对应的日期时间
    if (m_baseTimestampMs > 0) {
        qint64 currentTimestamp = m_baseTimestampMs + positionMs;
        m_timestampLabel->setText(formatDateTime(currentTimestamp) + " | " + tr("时间戳: %1").arg(currentTimestamp));
        
        // 检查是否需要加载下一个CAN log
        // 如果刚刚拖动过，使用拖动位置判断是否需要加载
        qint64 checkTimestamp = (m_lastSeekPositionMs >= 0) 
            ? (m_baseTimestampMs + m_lastSeekPositionMs) 
            : currentTimestamp;
        loadNextCanLogIfNeeded(checkTimestamp);
    } else {
        m_timestampLabel->setText(tr("时间戳: 偏移 %1 ms").arg(positionMs));
    }
    
    // 降低CAN显示更新频率：根据播放倍速动态调整
    // 目标：每500ms实际时间更新一次CAN显示
    // 0.01x倍速时threshold=5ms视频时间，1x倍速时threshold=500ms视频时间
    qreal rate = m_speedComboBox->currentData().toDouble();
    if (rate <= 0) rate = 1.0;
    qint64 thresholdMs = qMax(5LL, static_cast<qint64>(500 * rate));
    
    // 如果刚刚拖动过，使用拖动位置而非播放器位置（避免播放器seek延迟导致显示错误）
    qint64 displayPositionMs = (m_lastSeekPositionMs >= 0) ? m_lastSeekPositionMs : positionMs;
    qDebug() << "[updatePosition] positionMs=" << positionMs 
             << "m_lastSeekPositionMs=" << m_lastSeekPositionMs 
             << "displayPositionMs=" << displayPositionMs
             << "m_lastCanUpdateMs=" << m_lastCanUpdateMs
             << "rate=" << rate << "threshold=" << thresholdMs;
    if (displayPositionMs - m_lastCanUpdateMs >= thresholdMs || m_lastCanUpdateMs == -1) {
        m_lastCanUpdateMs = displayPositionMs;
        updateCanDisplay(displayPositionMs);
    }
}

void MainWindow::updateDuration(qint64 durationMs)
{
    m_durationMs = durationMs;
    m_positionSlider->setRange(0, static_cast<int>(std::min<qint64>(durationMs, std::numeric_limits<int>::max())));
    
    // 初始化时间显示
    if (m_baseTimestampMs > 0) {
        qint64 startTimestamp = m_baseTimestampMs;
        qint64 endTimestamp = m_baseTimestampMs + durationMs;
        qint64 remainingMs = durationMs;
        m_timeLabel->setText(tr("开始: %1 | 结束: %2 | 剩余: %3 ms")
            .arg(startTimestamp)
            .arg(endTimestamp)
            .arg(remainingMs));
    } else {
        m_timeLabel->setText(formatTime(m_player->position()) + " / " + formatTime(durationMs));
    }
}

void MainWindow::updateCanDisplay(qint64 positionMs)
{
    if (m_canFrames.isEmpty()) {
        m_canRawTableModel->setRows({});
        m_canTableModel->setRows({});
        return;
    }

    QElapsedTimer perfTimer;
    perfTimer.start();

    // 二分查找：当前时间点之前的所有CAN帧
    const auto it = std::upper_bound(m_canFrames.cbegin(), m_canFrames.cend(), positionMs,
                                     [](qint64 position, const CanFrame &frame) {
                                         return position < frame.timeMs;
                                     });
    
    int frameCount = std::distance(m_canFrames.cbegin(), it);
    qDebug() << "[updateCanDisplay] positionMs:" << positionMs 
             << "frameCount:" << frameCount 
             << "总帧数:" << m_canFrames.size()
             << "第一帧timeMs:" << m_canFrames.first().timeMs
             << "最后一帧timeMs:" << m_canFrames.last().timeMs;
    
    if (frameCount == 0) {
        m_canRawTableModel->setRows({});
        m_canTableModel->setRows({});
        m_canStatusLabel->setText(tr("CAN 数据：无"));
        return;
    }
    
    // 只遍历最近500帧（足以覆盖所有活跃ID）
    auto startIt = it;
    int framesToProcess = qMin(frameCount, 500);
    std::advance(startIt, -framesToProcess);
    
    // 按CAN ID分组，每个ID保留最新的帧
    QMap<quint32, const CanFrame*> latestFrames;
    for (auto frameIt = startIt; frameIt != it; ++frameIt) {
        latestFrames[frameIt->can_id] = &(*frameIt);
    }
    
    int currentUniqueCanIdCount = latestFrames.size();
    
    qint64 t1 = perfTimer.elapsed();
    
    // 构建PCAN-View表数据（QVector<QStringList>）
    QVector<QStringList> rawRows;
    rawRows.reserve(currentUniqueCanIdCount);
    
    // 同时收集信号解析结果
    QMap<QString, QPair<QString, QString>> allSignals;
    
    for (auto mapIt = latestFrames.begin(); mapIt != latestFrames.end(); ++mapIt) {
        const CanFrame *frame = mapIt.value();
        
        QStringList row;
        row.reserve(4);
        row << QString("0x%1").arg(frame->can_id, 0, 16);
        row << ((frame->can_id <= 0x7FF) ? QString::fromUtf8("标准帧") : QString::fromUtf8("扩展帧"));
        row << QString::number(frame->can_dlc);
        
        // 数据十六进制拼接
        QString dataHex;
        dataHex.reserve(frame->data.size() * 3);
        for (int i = 0; i < frame->data.size() && i < 8; ++i) {
            if (i > 0) dataHex += ' ';
            dataHex += QString("%1").arg(static_cast<unsigned char>(frame->data[i]), 2, 16, QChar('0'));
        }
        row << dataHex;
        rawRows.append(row);
        
        // 顺便解析信号
        QMap<QString, QPair<QString, QString>> frameSignals;
        parseCanSignals(frame->can_id, frame->data, frameSignals);
        for (auto sigIt = frameSignals.begin(); sigIt != frameSignals.end(); ++sigIt) {
            allSignals[sigIt.key()] = sigIt.value();
        }
    }
    
    qint64 t2 = perfTimer.elapsed();
    
    // 构建信号表数据
    QVector<QStringList> signalRows;
    signalRows.reserve(allSignals.size());
    for (auto sigIt = allSignals.begin(); sigIt != allSignals.end(); ++sigIt) {
        QStringList row;
        row.reserve(3);
        row << sigIt.key() << sigIt.value().first << sigIt.value().second;
        signalRows.append(row);
    }
    
    qint64 t3 = perfTimer.elapsed();
    
    // 一次性替换所有数据（关键：每个Model只触发一次重绘）
    m_canRawTableModel->setRows(rawRows);
    m_canTableModel->setRows(signalRows);
    
    qint64 t4 = perfTimer.elapsed();
    
    // 状态栏
    if (!latestFrames.isEmpty()) {
        const CanFrame &lastFrame = *latestFrames.last();
        qint64 originalAbsoluteTimestamp = lastFrame.timeMs + m_baseTimestampMs;
        
        QString logInfo;
        if (m_currentCanLogIndex >= 0 && m_currentCanLogIndex < m_canLogFileList.size()) {
            logInfo = tr(" | Log: %1/%2")
                .arg(m_currentCanLogIndex + 1)
                .arg(m_canLogFileList.size());
        }
        
        m_canStatusLabel->setText(tr("CAN 帧数：%1 | 最后更新：%2 (绝对: %3)%4")
            .arg(latestFrames.size())
            .arg(formatTime(lastFrame.timeMs))
            .arg(originalAbsoluteTimestamp)
            .arg(logInfo));
    }
    
    qint64 elapsed = perfTimer.elapsed();
    if (elapsed > 5) {
        qDebug() << "[updateCanDisplay] 总耗时:" << elapsed << "ms"
                 << "(分组:" << t1 << "raw构建:" << (t2-t1) << "信号构建:" << (t3-t2)
                 << "刷新:" << (t4-t3) << "ms)"
                 << "帧数:" << m_canFrames.size()
                 << "ID数:" << currentUniqueCanIdCount
                 << "positionMs:" << positionMs;
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

void MainWindow::clearCanCache()
{
    // 清空CAN帧数据
    m_canFrames.clear();
    
    // 清空表格显示
    if (m_canTableModel) {
        m_canTableModel->setRows({});
    }
    if (m_canRawTableModel) {
        m_canRawTableModel->setRows({});
    }
    
    // 重置CAN相关状态
    m_currentCanLogIndex = -1;
    m_canLogNextLoadTimestamp = 0;
    m_canLogFilePosition = 0;
    m_isLoadingCanData = false;
    m_lastCanUpdateMs = -1;  // 重置CAN更新频率限制
    
    // 更新状态标签
    if (m_canStatusLabel) {
        m_canStatusLabel->setText(tr("CAN 数据：无"));
    }
    
    qDebug() << "[clearCanCache] CAN数据缓存已清除";
}

bool MainWindow::parseCanLog(const QString &fileName, QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = tr("无法打开文件：%1").arg(file.errorString());
        return false;
    }

    m_canFrames.clear();
    
    QTextStream in(&file);
    int lineNumber = 0;
    
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        lineNumber++;
        
        if (line.isEmpty()) {
            continue;
        }
        
        CanFrame frame;
        QString lineError;
        if (parseCanLogLine(line, frame, &lineError)) {
            m_canFrames.append(frame);
        } else {
            // 静默跳过非CAN数据行（如CAN_DATA行、注释行等）
            // qWarning() << "Line" << lineNumber << ":" << lineError;
        }
    }

    if (m_canFrames.isEmpty()) {
        *error = tr("没有解析到有效的 CAN 数据");
        return false;
    }
    
    return true;
}

bool MainWindow::parseCanLogLine(const QString &line, CanFrame &frame, QString *error)
{
    // 新解析格式: 时间戳,负数ID,[data]
    // 例如: 1777291427473,-1744501455,[92, 0, -43, -1, -1, -1, 80, -91]
    
    // 分割时间戳、ID和data：格式为 "时间戳,负数ID,[data]"
    // 注意：需要找到第一个 '[' 来分割前面的部分和data数组
    int bracketStart = line.indexOf('[');
    int bracketEnd = line.lastIndexOf(']');
    
    if (bracketStart == -1 || bracketEnd == -1 || bracketEnd <= bracketStart) {
        *error = tr("找不到有效的data数组");
        qDebug() << "解析失败 - 找不到括号:" << line;
        return false;
    }
    
    // 提取时间戳和ID部分
    QString headerPart = line.left(bracketStart - 1).trimmed();  // "时间戳,负数ID"
    QStringList headerParts = headerPart.split(',');
    
    if (headerParts.size() < 2) {
        *error = tr("数据格式错误");
        qDebug() << "解析失败 - headerParts数量不足:" << headerParts.size() << line;
        return false;
    }
    
    // 解析时间戳（第一部分）
    QString timestampStr = headerParts[0].trimmed();
    bool ok;
    frame.timeMs = timestampStr.toLongLong(&ok);
    if (!ok) {
        *error = tr("时间戳解析失败");
        qDebug() << "解析失败 - 时间戳:" << timestampStr;
        return false;
    }
    
    // 解析CAN ID（第二部分，是负数）
    QString idStr = headerParts[1].trimmed();
    int signedId = idStr.toInt(&ok);
    if (!ok) {
        *error = tr("CAN ID解析失败");
        qDebug() << "解析失败 - CAN ID:" << idStr;
        return false;
    }
    quint32 unsignedId = static_cast<quint32>(signedId);
    // 扩展CAN帧ID只有29位，屏蔽掉高3位
    frame.can_id = unsignedId & 0x1FFFFFFF;
    
    // 解析data数组（有符号十进制数，需要转换为无符号字节）
    QString dataStr = line.mid(bracketStart + 1, bracketEnd - bracketStart - 1);
    QStringList dataValues = dataStr.split(',');
    
    for (int i = 0; i < dataValues.size() && i < 8; ++i) {
        int byteValue = dataValues[i].trimmed().toInt();
        frame.data.append(static_cast<char>(byteValue & 0xFF));
    }
    
    // 设置can_dlc为实际数据长度
    frame.can_dlc = static_cast<quint8>(qMin(dataValues.size(), 8));
    
    // 构建JSON payload用于显示
    QJsonObject jsonObj;
    jsonObj.insert("can_id", static_cast<qint64>(frame.can_id));
    jsonObj.insert("can_dlc", frame.can_dlc);
    
    QJsonArray dataArray;
    for (int i = 0; i < frame.data.size(); ++i) {
        dataArray.append(static_cast<int>(static_cast<unsigned char>(frame.data[i])));
    }
    jsonObj.insert("data", dataArray);
    
    frame.payload = jsonObj;
    
    return true;
}

qint64 MainWindow::parseTimeStringMs(const QString &value) const
{
    const QString trimmed = value.trimmed();
    bool ok = false;
    const double seconds = trimmed.toDouble(&ok);
    if (ok) {
        return static_cast<qint64>(seconds * 1000.0);
    }

    const QTime time = QTime::fromString(trimmed, "HH:mm:ss.zzz");
    if (time.isValid()) {
        return QTime(0, 0).msecsTo(time);
    }

    const QTime secondTime = QTime::fromString(trimmed, "HH:mm:ss");
    if (secondTime.isValid()) {
        return QTime(0, 0).msecsTo(secondTime);
    }

    return -1;
}

QString MainWindow::formatTime(qint64 ms) const
{
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    const qint64 millis = ms % 1000;
    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString MainWindow::formatDateTime(qint64 timestampMs) const
{
    // 将毫秒时间戳转换为QDateTime
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    // 格式：yy/MM/dd HH:mm:ss.zzz
    return dateTime.toString("yy/MM/dd HH:mm:ss.zzz");
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

float MainWindow::extractBits(const QByteArray &data, int start_bit, int len, bool is_intel)
{
    if (data.size() < 8 || len <= 0 || len > 32 || start_bit < 0 || start_bit + len > 64)
        return 0;

    quint64 raw = 0;
    const quint8 *bytes = reinterpret_cast<const quint8*>(data.constData());
    
    if (is_intel) {
        // Intel格式（小端）
        for (int i = 0; i < 8; i++)
            raw |= (quint64)bytes[i] << (8 * i);
    } else {
        // Motorola格式（大端）
        for (int i = 0; i < 8; i++)
            raw = (raw << 8) | bytes[i];
        start_bit = 63 - start_bit;
    }

    quint64 mask = (1ULL << len) - 1;
    return (float)((raw >> start_bit) & mask);
}

void MainWindow::parseCanSignals(quint32 can_id, const QByteArray &data, QMap<QString, QPair<QString, QString>> &signalList)
{
    // 根据CAN ID解析信号（基于can_parser.cpp中的定义）
    struct SignalDef {
        quint32 can_id;
        QString name;
        QString chineseName;
        int start_bit;
        int bit_len;
        float factor;
        float offset;
        bool is_intel;
        QString unit;
    };
    
    static const QVector<SignalDef> signalDefs = {
        // IMU传感器
        {0x18050531, "IMU", "横滚角", 0, 16, 0.0054932f, 0, true, "°"},
        {0x18050531, "IMU", "俯仰角", 16, 16, 0.0054932f, 0, true, "°"},
        {0x18050531, "IMU", "偏航角", 32, 16, 0.0054932f, 0, true, "°"},
        
        {0x18050631, "IMU", "X轴加速度", 0, 16, 0.00024414f, 0, true, "G"},
        {0x18050631, "IMU", "Y轴加速度", 16, 16, 0.00024414f, 0, true, "G"},
        {0x18050631, "IMU", "Z轴加速度", 32, 16, 0.00024414f, 0, true, "G"},
        {0x18050631, "IMU", "温度", 48, 16, 0.0045776f, 0, true, "℃"},
        
        {0x18050731, "IMU", "X轴角速度", 0, 16, 0.0076294f, 0, true, "°/s"},
        {0x18050731, "IMU", "Y轴角速度", 16, 16, 0.0076294f, 0, true, "°/s"},
        {0x18050731, "IMU", "Z轴角速度", 32, 16, 0.0076294f, 0, true, "°/s"},
        
        // 油装系统常用信号（示例）
        {0x18FF481E, "CAR", "发动机转速", 0, 16, 0.125f, 0, true, "RPM"},
        {0x18FF481E, "CAR", "燃油液位", 16, 8, 0.4f, 0, true, "%"},
        {0x18FF481E, "CAR", "车速", 24, 16, 0.00390625f, 0, true, "KM/h"},
        {0x18FF481E, "CAR", "变矩器油温", 48, 8, 1.0f, -40, true, "℃"},
        {0x18FF481E, "CAR", "发动机水温", 56, 8, 1.0f, -40, true, "℃"},
    };
    
    // 遍历匹配的信号定义
    for (const auto &sig : signalDefs) {
        if (sig.can_id != can_id) continue;
        
        // 提取原始值并计算物理值
        float rawValue = extractBits(data, sig.start_bit, sig.bit_len, sig.is_intel);
        float physicalValue = rawValue * sig.factor + sig.offset;
        
        // 分别存储值和单位
        QString valueStr = QString("%1 (%2)").arg(physicalValue, 0, 'f', 2).arg(sig.name);
        signalList[sig.chineseName] = qMakePair(valueStr, sig.unit);
    }
}

// ========== 文件夹选择和视频列表功能 ==========

void MainWindow::openVideoFolder()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择视频文件夹"), "../..");
    if (folderPath.isEmpty()) {
        return;
    }
    
    m_videoFolderPath = folderPath;
    setStatus(tr("已选择视频文件夹: %1").arg(folderPath));
    
    // 根据当前选择的日期加载视频列表
    loadVideoList(m_currentDate);
}

void MainWindow::openCanLogFolder()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("选择CAN Log文件夹"), "../..");
    if (folderPath.isEmpty()) {
        return;
    }
    
    m_canLogFolderPath = folderPath;
    setStatus(tr("已选择CAN Log文件夹: %1").arg(folderPath));
    
    // 加载CAN log文件列表
    loadCanLogFolder();
}

void MainWindow::onDateChanged(const QDate &date)
{
    // 日期改变时，重新加载视频列表
    if (!m_videoFolderPath.isEmpty()) {
        loadVideoList(date);
    }
}

void MainWindow::onVideoSelected(int row)
{
    if (row < 0 || row >= m_videoFileList.size()) {
        return;
    }
    
    QString videoFile = m_videoFileList[row];
    
    // 从文件名提取时间戳（文件名就是时间戳）
    QFileInfo fileInfo(videoFile);
    QString baseName = fileInfo.baseName();
    qint64 timestamp = baseName.toLongLong();
    
    // ===== 清理旧的CAN数据缓存 =====
    clearCanCache();
    
    // 加载视频
    m_player->setSource(QUrl::fromLocalFile(videoFile));
    m_player->play();
    
    // 不调用setPlaybackUi，由playbackStateChanged信号自动更新
    
    // 更新基础时间戳
    m_baseTimestampMs = timestamp;
    
    // 自动匹配最接近的CAN log
    if (!m_canLogFolderPath.isEmpty()) {
        loadCanLogByTimestamp(timestamp);
    }
    
    setStatus(tr("已加载视频: %1").arg(fileInfo.fileName()));
}

void MainWindow::loadVideoList(const QDate &date)
{
    // 切换日期时停止当前播放并清空CAN缓存
    m_player->stop();
    clearCanCache();
    
    m_videoFileList.clear();
    m_videoListWidget->clear();
    
    if (m_videoFolderPath.isEmpty()) {
        m_videoListWidget->hide();
        return;
    }
    
    QDir dir(m_videoFolderPath);
    QStringList filters;
    filters << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv";
    
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);
    
    // 计算选中日期的开始和结束时间戳
    qint64 dayStart = timestampFromDate(date);
    qint64 dayEnd = dayStart + 24 * 60 * 60 * 1000;  // 加一天
    
    // 过滤出该日期的视频
    for (const auto &file : fileList) {
        QString baseName = file.baseName();
        bool ok;
        qint64 timestamp = baseName.toLongLong(&ok);
        
        if (ok && timestamp >= dayStart && timestamp < dayEnd) {
            m_videoFileList.append(file.absoluteFilePath());
            
            // 显示格式：HH:mm:ss - 文件名
            QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestamp);
            QString displayText = QString("%1 - %2")
                .arg(dateTime.toString("HH:mm:ss"))
                .arg(file.fileName());
            
            m_videoListWidget->addItem(displayText);
        }
    }
    
    if (m_videoFileList.isEmpty()) {
        m_videoListWidget->addItem(tr("该日期没有视频文件"));
        m_videoListWidget->hide();
        setStatus(tr("%1 没有找到视频文件").arg(date.toString("yyyy-MM-dd")));
    } else {
        m_videoListWidget->show();
        setStatus(tr("找到 %1 个视频文件").arg(m_videoFileList.size()));
    }
}

void MainWindow::loadCanLogFolder()
{
    m_canLogFileList.clear();
    
    if (m_canLogFolderPath.isEmpty()) {
        return;
    }
    
    QDir dir(m_canLogFolderPath);
    QStringList filters;
    filters << "*.log";
    
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);
    
    for (const auto &file : fileList) {
        QString baseName = file.baseName();
        bool ok;
        baseName.toLongLong(&ok);  // 文件名应该是时间戳
        
        if (ok) {
            m_canLogFileList.append(file.absoluteFilePath());
        }
    }
    
    // 按时间戳排序（文件名就是时间戳）
    std::sort(m_canLogFileList.begin(), m_canLogFileList.end(), 
        [](const QString &a, const QString &b) {
            return QFileInfo(a).baseName().toLongLong() < QFileInfo(b).baseName().toLongLong();
        });
    
    setStatus(tr("找到 %1 个CAN log文件").arg(m_canLogFileList.size()));
}

bool MainWindow::loadCanLogByTimestamp(qint64 timestamp, qint64 targetPositionMs)
{
    if (m_canLogFileList.isEmpty()) {
        setStatus(tr("没有可用的CAN log文件"));
        return false;
    }
    
    // 如果正在加载，不重复触发
    if (m_isLoadingCanData) {
        return false;
    }
    
    // 找到最接近（小于等于）该时间戳的log文件
    int bestIndex = -1;
    qint64 bestDiff = -1;
    
    for (int i = 0; i < m_canLogFileList.size(); ++i) {
        qint64 logTimestamp = QFileInfo(m_canLogFileList[i]).baseName().toLongLong();
        
        if (logTimestamp <= timestamp) {
            if (bestIndex == -1 || (timestamp - logTimestamp) < bestDiff) {
                bestIndex = i;
                bestDiff = timestamp - logTimestamp;
            }
        }
    }
    
    if (bestIndex == -1) {
        setStatus(tr("没有找到匹配的CAN log文件"));
        return false;
    }
    
    // 加载该log文件（异步解析，不阻塞UI）
    m_currentCanLogIndex = bestIndex;
    QString logFile = m_canLogFileList[bestIndex];
    
    setStatus(tr("正在解析CAN log: %1...").arg(QFileInfo(logFile).fileName()));
    
    // 标记正在加载
    m_isLoadingCanData = true;
    
    // 计算预加载范围（以targetPositionMs为中心，前后各加载一段）
    qint64 absoluteTarget = timestamp + targetPositionMs;
    qint64 preloadStart = absoluteTarget - 5000;   // 目标位置前5秒
    qint64 preloadEnd = absoluteTarget + 60000;    // 目标位置后60秒
    
    // 使用QtConcurrent在后台线程解析
    QFuture<bool> future = QtConcurrent::run([this, logFile, timestamp, preloadStart, preloadEnd]() {
        QFile file(logFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return false;
        }
        
        QVector<CanFrame> frames;
        QTextStream in(&file);
        
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            
            // 快速提取时间戳（新格式：时间戳,负数ID,[data]）
            int commaPos = line.indexOf(',');
            if (commaPos == -1) continue;
            
            QString timestampStr = line.left(commaPos).trimmed();
            
            bool ok;
            qint64 lineTimestamp = timestampStr.toLongLong(&ok);
            if (!ok) continue;
            
            // 只加载预加载范围内的数据
            if (lineTimestamp >= preloadStart && lineTimestamp <= preloadEnd) {
                CanFrame frame;
                QString lineError;
                if (parseCanLogLine(line, frame, &lineError)) {
                    frame.timeMs = frame.timeMs - timestamp;  // 转换为相对于视频开始的时间
                    frames.append(frame);
                }
            }
            
            // 如果已经超过了预加载范围，停止读取
            if (lineTimestamp > preloadEnd) {
                break;
            }
        }
        
        // 保存文件位置和预加载的结束位置，供后续加载
        QMetaObject::invokeMethod(this, [this, frames, preloadEnd, timestamp]() {
            m_canFrames = frames;
            m_canLogNextLoadTimestamp = preloadEnd;  // 下次从这里开始加载
            m_canLogFilePosition = 0;  // 重置（如果需要重新读取）
            m_isLoadingCanData = false;  // 重置加载标志
            
            qDebug() << "[loadCanLogByTimestamp] 加载完成, 帧数:" << frames.size();
            if (!frames.isEmpty()) {
                qDebug() << "[loadCanLogByTimestamp] 相对时间戳范围:" 
                         << frames.first().timeMs << "~" << frames.last().timeMs;
                qDebug() << "[loadCanLogByTimestamp] 视频开始绝对时间戳:" << timestamp;
            }
        }, Qt::QueuedConnection);
        
        return true;
    });
    
    // 等待解析完成，但不阻塞事件循环
    QFutureWatcher<bool> *watcher = new QFutureWatcher<bool>();
    watcher->setFuture(future);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, logFile, bestDiff, targetPositionMs]() {
        bool success = watcher->result();
        watcher->deleteLater();
        
        if (success) {
            qDebug() << "CAN log加载完成，帧数:" << m_canFrames.size();
            if (!m_canFrames.isEmpty()) {
                qDebug() << "第一帧时间戳:" << m_canFrames.first().timeMs;
                qDebug() << "最后一帧时间戳:" << m_canFrames.last().timeMs;
                qDebug() << "CAN ID示例:" << QString("0x%1").arg(m_canFrames.first().can_id, 0, 16);
            }
            
            setStatus(tr("已加载CAN log: %1 (差异: %2 ms, %3 帧)")
                .arg(QFileInfo(logFile).fileName())
                .arg(bestDiff)
                .arg(m_canFrames.size()));
            
            // 加载完成后立即更新CAN显示
            // 优先使用拖动位置，其次使用加载时指定的目标位置
            qint64 displayPos = (m_lastSeekPositionMs >= 0) ? m_lastSeekPositionMs : targetPositionMs;
            updateCanDisplay(displayPos);
        } else {
            m_isLoadingCanData = false;  // 加载失败也要重置标志
            setStatus(tr("CAN log加载失败"));
        }
    });
    
    return true;
}

void MainWindow::loadNextCanLogIfNeeded(qint64 currentAbsoluteTimestamp)
{
    // 当前CAN数据已经不够用了，检查是否需要加载下一个log
    if (m_canFrames.isEmpty() || m_currentCanLogIndex < 0) {
        return;
    }
    
    // 如果正在加载，不重复触发
    if (m_isLoadingCanData) {
        return;
    }
    
    // 检查当前CAN数据的最后一帧的绝对时间戳
    qint64 lastCanRelativeMs = m_canFrames.last().timeMs;
    qint64 lastCanAbsoluteTimestamp = m_baseTimestampMs + lastCanRelativeMs;
    
    // 检测是否需要加载后续数据（当前位置距离已加载数据末尾不到10秒）
    // 注意：只有真正接近末尾时才加载，不要每次都要重新加载！
    qint64 timeToEnd = lastCanAbsoluteTimestamp - currentAbsoluteTimestamp;
    
    if (timeToEnd < 10000 && timeToEnd > -1000) {  // 距离末尾10秒内，且没有超过末尾1秒以上
        // 需要加载后续数据
        qDebug() << "[loadNextCanLogIfNeeded] 接近末尾，加载后续数据, timeToEnd:" << timeToEnd;
        
        if (m_currentCanLogIndex < m_canLogFileList.size()) {
            QString currentLogFile = m_canLogFileList[m_currentCanLogIndex];
            
            // 异步加载后续60秒的数据
            qint64 loadStart = m_canLogNextLoadTimestamp;
            qint64 loadEnd = loadStart + 60000;  // 加载60秒
            
            qDebug() << "[loadNextCanLogIfNeeded] 加载后续数据:" << loadStart << "~" << loadEnd;
            
            // 标记正在加载
            m_isLoadingCanData = true;
            
            QFuture<bool> future = QtConcurrent::run([this, currentLogFile, loadStart, loadEnd]() {
                QFile file(currentLogFile);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    return false;
                }
                
                QVector<CanFrame> newFrames;
                QTextStream in(&file);
                bool foundStart = false;
                
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.isEmpty()) continue;
                    
                    // 快速提取时间戳（新格式：时间戳,负数ID,[data]）
                    int commaPos = line.indexOf(',');
                    if (commaPos == -1) continue;
                    
                    QString timestampStr = line.left(commaPos).trimmed();
                    
                    bool ok;
                    qint64 lineTimestamp = timestampStr.toLongLong(&ok);
                    if (!ok) continue;
                    
                    // 跳过加载范围之前的数据
                    if (lineTimestamp < loadStart) {
                        continue;
                    }
                    
                    // 加载范围内的数据
                    if (lineTimestamp >= loadStart && lineTimestamp <= loadEnd) {
                        CanFrame frame;
                        QString lineError;
                        if (parseCanLogLine(line, frame, &lineError)) {
                            frame.timeMs = frame.timeMs - m_baseTimestampMs;
                            newFrames.append(frame);
                        }
                    }
                    
                    // 超过范围，停止
                    if (lineTimestamp > loadEnd) {
                        break;
                    }
                }
                
                // 追加到现有数据
                if (!newFrames.isEmpty()) {
                    QMetaObject::invokeMethod(this, [this, newFrames, loadEnd]() {
                        // 合并两个有序序列（m_canFrames和newFrames都是按timeMs排序的）
                        // 使用std::merge而不是append+sort
                        QVector<CanFrame> merged;
                        merged.reserve(m_canFrames.size() + newFrames.size());
                        std::merge(m_canFrames.cbegin(), m_canFrames.cend(),
                                   newFrames.cbegin(), newFrames.cend(),
                                   std::back_inserter(merged),
                                   [](const CanFrame &a, const CanFrame &b) {
                                       return a.timeMs < b.timeMs;
                                   });
                        m_canFrames = std::move(merged);
                        m_canLogNextLoadTimestamp = loadEnd;
                        m_isLoadingCanData = false;  // 重置加载标志
                        
                        // 加载完成后更新CAN显示
                        // 如果刚刚拖动过，使用拖动位置而非播放器位置
                        qint64 displayPos = (m_lastSeekPositionMs >= 0) ? m_lastSeekPositionMs : m_player->position();
                        updateCanDisplay(displayPos);
                    }, Qt::QueuedConnection);
                } else {
                    // 即使没有新数据，也要重置标志
                    QMetaObject::invokeMethod(this, [this]() {
                        m_isLoadingCanData = false;
                    }, Qt::QueuedConnection);
                }
                
                return true;
            });
            
            // 更新下次加载的时间戳
            m_canLogNextLoadTimestamp = loadEnd;
        } else if (m_currentCanLogIndex + 1 < m_canLogFileList.size()) {
            // 当前log已经读完，加载下一个log文件
            m_currentCanLogIndex++;
            QString nextLogFile = m_canLogFileList[m_currentCanLogIndex];
            
            setStatus(tr("自动加载下一个CAN log: %1").arg(QFileInfo(nextLogFile).fileName()));
            
            // 重新调用loadCanLogByTimestamp加载新文件
            // 使用视频基准时间戳作为时间基准
            loadCanLogByTimestamp(m_baseTimestampMs);
        }
    }
}
