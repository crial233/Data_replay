#include "can_data_manager.h"
#include "simple_table_model.h"

#include <QTableView>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTime>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QFutureWatcher>

#include <algorithm>

CanDataManager::CanDataManager(QObject *parent)
    : QObject(parent)
{
}

void CanDataManager::setCanTables(QTableView *canTable, QTableView *canRawTable,
                                  SimpleTableModel *canTableModel, SimpleTableModel *canRawTableModel)
{
    m_canTable = canTable;
    m_canRawTable = canRawTable;
    m_canTableModel = canTableModel;
    m_canRawTableModel = canRawTableModel;
}

void CanDataManager::setCanLogFolderPath(const QString &path)
{
    m_canLogFolderPath = path;
}

// ===== CAN log解析 =====

bool CanDataManager::parseCanLog(const QString &fileName, QString *error)
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
        }
    }

    if (m_canFrames.isEmpty()) {
        *error = tr("没有解析到有效的 CAN 数据");
        return false;
    }
    
    return true;
}

bool CanDataManager::parseCanLogLine(const QString &line, CanFrame &frame, QString *error)
{
    // 新解析格式: 时间戳,负数ID,[data]
    // 例如: 1777291427473,-1744501455,[92, 0, -43, -1, -1, -1, 80, -91]
    
    int bracketStart = line.indexOf('[');
    int bracketEnd = line.lastIndexOf(']');
    
    if (bracketStart == -1 || bracketEnd == -1 || bracketEnd <= bracketStart) {
        *error = tr("找不到有效的data数组");
        qDebug() << "解析失败 - 找不到括号:" << line;
        return false;
    }
    
    // 提取时间戳和ID部分
    QString headerPart = line.left(bracketStart - 1).trimmed();
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
    frame.originalTimeMs = frame.timeMs;
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

qint64 CanDataManager::parseTimeStringMs(const QString &value) const
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

// ===== CAN信号解析 =====

float CanDataManager::extractBits(const QByteArray &data, int start_bit, int len, bool is_intel)
{
    if (data.size() < 8 || len <= 0 || len > 32 || start_bit < 0 || start_bit + len > 64)
        return 0;

    quint64 raw = 0;
    const quint8 *bytes = reinterpret_cast<const quint8*>(data.constData());
    
    if (is_intel) {
        for (int i = 0; i < 8; i++)
            raw |= (quint64)bytes[i] << (8 * i);
    } else {
        for (int i = 0; i < 8; i++)
            raw = (raw << 8) | bytes[i];
        start_bit = 63 - start_bit;
    }

    quint64 mask = (1ULL << len) - 1;
    return (float)((raw >> start_bit) & mask);
}

void CanDataManager::parseCanSignals(quint32 can_id, const QByteArray &data,
                                     QMap<QString, QPair<QString, QString>> &signalList)
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
        
        // 油装系统常用信号
        {0x18FF481E, "CAR", "发动机转速", 0, 16, 0.125f, 0, true, "RPM"},
        {0x18FF481E, "CAR", "燃油液位", 16, 8, 0.4f, 0, true, "%"},
        {0x18FF481E, "CAR", "车速", 24, 16, 0.00390625f, 0, true, "KM/h"},
        {0x18FF481E, "CAR", "变矩器油温", 48, 8, 1.0f, -40, true, "℃"},
        {0x18FF481E, "CAR", "发动机水温", 56, 8, 1.0f, -40, true, "℃"},
    };
    
    // 遍历匹配的信号定义
    for (const auto &sig : signalDefs) {
        if (sig.can_id != can_id) continue;
        
        float rawValue = extractBits(data, sig.start_bit, sig.bit_len, sig.is_intel);
        float physicalValue = rawValue * sig.factor + sig.offset;
        
        QString valueStr = QString("%1 (%2)").arg(physicalValue, 0, 'f', 2).arg(sig.name);
        signalList[sig.chineseName] = qMakePair(valueStr, sig.unit);
    }
}

// ===== CAN显示更新 =====

void CanDataManager::updateCanDisplay(qint64 positionMs)
{
    if (m_canFrames.isEmpty()) {
        if (m_canRawTableModel) m_canRawTableModel->setRows({});
        if (m_canTableModel) m_canTableModel->setRows({});
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
        if (m_canRawTableModel) m_canRawTableModel->setRows({});
        if (m_canTableModel) m_canTableModel->setRows({});
        emit statusMessage(tr("CAN 数据：无"));
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
    
    // 构建PCAN-View表数据
    QVector<QStringList> rawRows;
    rawRows.reserve(currentUniqueCanIdCount);
    
    struct SignalDisplay {
        qint64 timeMs = 0;
        QString value;
        QString unit;
    };

    // 同时收集信号解析结果
    QMap<QString, SignalDisplay> allSignals;
    
    for (auto mapIt = latestFrames.begin(); mapIt != latestFrames.end(); ++mapIt) {
        const CanFrame *frame = mapIt.value();
        
        QStringList row;
        row.reserve(3);
        row << QString::number(frame->originalTimeMs);
        row << QString("0x%1").arg(frame->can_id, 0, 16);
        
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
            allSignals[sigIt.key()] = SignalDisplay{frame->timeMs, sigIt.value().first, sigIt.value().second};
        }
    }
    
    qint64 t2 = perfTimer.elapsed();
    
    // 构建信号表数据
    QVector<QStringList> signalRows;
    signalRows.reserve(allSignals.size());
    for (auto sigIt = allSignals.begin(); sigIt != allSignals.end(); ++sigIt) {
        QStringList row;
        row.reserve(4);
        row << formatTime(sigIt.value().timeMs)
            << sigIt.key()
            << sigIt.value().value
            << sigIt.value().unit;
        signalRows.append(row);
    }
    
    qint64 t3 = perfTimer.elapsed();
    
    // 一次性替换所有数据
    if (m_canRawTableModel) m_canRawTableModel->setRows(rawRows);
    if (m_canTableModel) m_canTableModel->setRows(signalRows);
    
    qint64 t4 = perfTimer.elapsed();
    
    // 状态信息
    if (!latestFrames.isEmpty()) {
        const CanFrame &lastFrame = *latestFrames.last();
        qint64 originalAbsoluteTimestamp = lastFrame.timeMs + m_baseTimestampMs;
        
        QString logInfo;
        if (m_currentCanLogIndex >= 0 && m_currentCanLogIndex < m_canLogFileList.size()) {
            logInfo = tr(" | Log: %1/%2")
                .arg(m_currentCanLogIndex + 1)
                .arg(m_canLogFileList.size());
        }
        
        emit statusMessage(tr("CAN 帧数：%1 | 最后更新：%2 (绝对: %3)%4")
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

QVector<QStringList> CanDataManager::rawFrameRowsAround(qint64 positionMs,
                                                        qint64 rangeMs,
                                                        qint64 nextFileThresholdMs)
{
    QVector<CanFrame> frames = m_canFrames;
    if (frames.isEmpty()) {
        return {};
    }
    const qint64 startTimeMs = positionMs - rangeMs;
    const qint64 endTimeMs = positionMs + rangeMs;

    auto makeRow = [this](const CanFrame &frame) {
        QString dataHex;
        dataHex.reserve(frame.data.size() * 3);
        for (int i = 0; i < frame.data.size() && i < 8; ++i) {
            if (i > 0) dataHex += ' ';
            dataHex += QString("%1").arg(static_cast<unsigned char>(frame.data[i]), 2, 16, QChar('0'));
        }

        QMap<QString, QPair<QString, QString>> parsedSignals;
        parseCanSignals(frame.can_id, frame.data, parsedSignals);
        QStringList parsedParts;
        for (auto sigIt = parsedSignals.begin(); sigIt != parsedSignals.end(); ++sigIt) {
            parsedParts << QString("%1=%2 %3")
                .arg(sigIt.key())
                .arg(sigIt.value().first)
                .arg(sigIt.value().second);
        }

        return QStringList{
            formatTime(frame.timeMs),
            QString::number(frame.originalTimeMs),
            QString("0x%1").arg(frame.can_id, 0, 16),
            dataHex,
            parsedParts.join("; ")
        };
    };

    auto lowerIt = std::lower_bound(frames.cbegin(), frames.cend(), startTimeMs,
                                    [](const CanFrame &frame, qint64 position) {
                                        return frame.timeMs < position;
                                    });
    if (frames.last().timeMs < endTimeMs && m_currentCanLogIndex + 1 < m_canLogFileList.size()) {
        const QString nextLogFile = m_canLogFileList[m_currentCanLogIndex + 1];
        bool ok = false;
        const qint64 nextFileTimestamp = QFileInfo(nextLogFile).baseName().toLongLong(&ok);
        const qint64 lastLoadedOriginal = frames.last().originalTimeMs;

        if (ok && nextFileTimestamp >= lastLoadedOriginal &&
            nextFileTimestamp - lastLoadedOriginal <= nextFileThresholdMs) {
            QFile file(nextLogFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                const qint64 startOriginalTime = m_baseTimestampMs + startTimeMs;
                const qint64 endOriginalTime = m_baseTimestampMs + endTimeMs;

                while (!in.atEnd()) {
                    const QString line = in.readLine().trimmed();
                    if (line.isEmpty()) {
                        continue;
                    }

                    const int commaPos = line.indexOf(',');
                    if (commaPos < 0) {
                        continue;
                    }

                    bool lineOk = false;
                    const qint64 lineTimestamp = line.left(commaPos).trimmed().toLongLong(&lineOk);
                    if (!lineOk || lineTimestamp < startOriginalTime) {
                        continue;
                    }
                    if (lineTimestamp > endOriginalTime) {
                        break;
                    }

                    CanFrame frame;
                    QString lineError;
                    if (parseCanLogLine(line, frame, &lineError)) {
                        frame.timeMs = frame.timeMs - m_baseTimestampMs;
                        frames.append(frame);
                    }
                }
            }
        }
    }

    lowerIt = std::lower_bound(frames.cbegin(), frames.cend(), startTimeMs,
                               [](const CanFrame &frame, qint64 position) {
                                   return frame.timeMs < position;
                               });

    auto endIt = std::upper_bound(frames.cbegin(), frames.cend(), endTimeMs,
                                  [](qint64 position, const CanFrame &frame) {
                                      return position < frame.timeMs;
                                  });

    QVector<QStringList> rows;
    rows.reserve(std::distance(lowerIt, endIt));
    for (auto it = lowerIt; it != endIt; ++it) {
        rows.append(makeRow(*it));
    }

    return rows;
}

// ===== CAN缓存清除 =====

void CanDataManager::clearCanCache()
{
    m_canFrames.clear();
    
    if (m_canTableModel) {
        m_canTableModel->setRows({});
    }
    if (m_canRawTableModel) {
        m_canRawTableModel->setRows({});
    }
    
    m_currentCanLogIndex = -1;
    m_canLogNextLoadTimestamp = 0;
    m_canLogFilePosition = 0;
    m_isLoadingCanData = false;
    m_lastCanUpdateMs = -1;
    
    emit statusMessage(tr("CAN 数据：无"));
    
    qDebug() << "[clearCanCache] CAN数据缓存已清除";
}

// ===== CAN log文件夹加载 =====

void CanDataManager::loadCanLogFolder()
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
        baseName.toLongLong(&ok);
        
        if (ok) {
            m_canLogFileList.append(file.absoluteFilePath());
        }
    }
    
    // 按时间戳排序
    std::sort(m_canLogFileList.begin(), m_canLogFileList.end(), 
        [](const QString &a, const QString &b) {
            return QFileInfo(a).baseName().toLongLong() < QFileInfo(b).baseName().toLongLong();
        });
    
    emit statusMessage(tr("找到 %1 个CAN log文件").arg(m_canLogFileList.size()));
}

// ===== 按时间戳加载CAN log =====

bool CanDataManager::loadCanLogByTimestamp(qint64 timestamp, qint64 targetPositionMs)
{
    if (m_canLogFileList.isEmpty()) {
        emit statusMessage(tr("没有可用的CAN log文件"));
        return false;
    }
    
    if (m_isLoadingCanData) {
        return false;
    }
    
    const qint64 absoluteTarget = timestamp + targetPositionMs;

    // 找到最接近（小于等于）目标绝对时间的log文件
    int bestIndex = -1;
    qint64 bestDiff = -1;
    
    for (int i = 0; i < m_canLogFileList.size(); ++i) {
        qint64 logTimestamp = QFileInfo(m_canLogFileList[i]).baseName().toLongLong();
        
        if (logTimestamp <= absoluteTarget) {
            if (bestIndex == -1 || (absoluteTarget - logTimestamp) < bestDiff) {
                bestIndex = i;
                bestDiff = absoluteTarget - logTimestamp;
            }
        }
    }
    
    if (bestIndex == -1) {
        emit statusMessage(tr("没有找到匹配的CAN log文件"));
        return false;
    }
    
    // 加载该log文件（异步解析）
    m_currentCanLogIndex = bestIndex;
    QString logFile = m_canLogFileList[bestIndex];
    
    emit statusMessage(tr("正在解析CAN log: %1...").arg(QFileInfo(logFile).fileName()));
    
    m_isLoadingCanData = true;
    
    // 计算预加载范围
    qint64 preloadStart = absoluteTarget - 5000;
    qint64 preloadEnd = absoluteTarget + 60000;
    
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
            
            int commaPos = line.indexOf(',');
            if (commaPos == -1) continue;
            
            QString timestampStr = line.left(commaPos).trimmed();
            
            bool ok;
            qint64 lineTimestamp = timestampStr.toLongLong(&ok);
            if (!ok) continue;
            
            if (lineTimestamp >= preloadStart && lineTimestamp <= preloadEnd) {
                CanFrame frame;
                QString lineError;
                if (parseCanLogLine(line, frame, &lineError)) {
                    frame.timeMs = frame.timeMs - m_baseTimestampMs;
                    frames.append(frame);
                }
            }
            
            if (lineTimestamp > preloadEnd) {
                break;
            }
        }
        
        QMetaObject::invokeMethod(this, [this, frames, preloadEnd, timestamp]() {
            m_canFrames = frames;
            m_canLogNextLoadTimestamp = preloadEnd;
            m_canLogFilePosition = 0;
            m_isLoadingCanData = false;
            
            qDebug() << "[loadCanLogByTimestamp] 加载完成, 帧数:" << frames.size();
            if (!frames.isEmpty()) {
                qDebug() << "[loadCanLogByTimestamp] 相对时间戳范围:" 
                         << frames.first().timeMs << "~" << frames.last().timeMs;
                qDebug() << "[loadCanLogByTimestamp] 视频开始绝对时间戳:" << timestamp;
            }
        }, Qt::QueuedConnection);
        
        return true;
    });
    
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
            
            emit statusMessage(tr("已加载CAN log: %1 (差异: %2 ms, %3 帧)")
                .arg(QFileInfo(logFile).fileName())
                .arg(bestDiff)
                .arg(m_canFrames.size()));
            
            emit canLogLoaded(QFileInfo(logFile).fileName(), m_canFrames.size(), bestDiff);
            
            qint64 displayPos = (m_lastSeekPositionMs >= 0) ? m_lastSeekPositionMs : targetPositionMs;
            updateCanDisplay(displayPos);
        } else {
            m_isLoadingCanData = false;
            emit statusMessage(tr("CAN log加载失败"));
        }
    });
    
    return true;
}

// ===== 加载后续CAN数据 =====

void CanDataManager::loadNextCanLogIfNeeded(qint64 currentAbsoluteTimestamp)
{
    if (m_canFrames.isEmpty() || m_currentCanLogIndex < 0) {
        return;
    }
    
    if (m_isLoadingCanData) {
        return;
    }
    
    qint64 lastCanRelativeMs = m_canFrames.last().timeMs;
    qint64 lastCanAbsoluteTimestamp = m_baseTimestampMs + lastCanRelativeMs;
    
    qint64 timeToEnd = lastCanAbsoluteTimestamp - currentAbsoluteTimestamp;
    
    if (timeToEnd < 10000 && timeToEnd > -1000) {
        qDebug() << "[loadNextCanLogIfNeeded] 接近末尾，加载后续数据, timeToEnd:" << timeToEnd;
        
        if (m_currentCanLogIndex < m_canLogFileList.size()) {
            QString currentLogFile = m_canLogFileList[m_currentCanLogIndex];
            
            qint64 loadStart = m_canLogNextLoadTimestamp;
            qint64 loadEnd = loadStart + 60000;
            
            qDebug() << "[loadNextCanLogIfNeeded] 加载后续数据:" << loadStart << "~" << loadEnd;
            
            m_isLoadingCanData = true;
            
            QFuture<bool> future = QtConcurrent::run([this, currentLogFile, loadStart, loadEnd]() {
                QFile file(currentLogFile);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    return false;
                }
                
                QVector<CanFrame> newFrames;
                QTextStream in(&file);
                
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.isEmpty()) continue;
                    
                    int commaPos = line.indexOf(',');
                    if (commaPos == -1) continue;
                    
                    QString timestampStr = line.left(commaPos).trimmed();
                    
                    bool ok;
                    qint64 lineTimestamp = timestampStr.toLongLong(&ok);
                    if (!ok) continue;
                    
                    if (lineTimestamp < loadStart) {
                        continue;
                    }
                    
                    if (lineTimestamp >= loadStart && lineTimestamp <= loadEnd) {
                        CanFrame frame;
                        QString lineError;
                        if (parseCanLogLine(line, frame, &lineError)) {
                            frame.timeMs = frame.timeMs - m_baseTimestampMs;
                            newFrames.append(frame);
                        }
                    }
                    
                    if (lineTimestamp > loadEnd) {
                        break;
                    }
                }
                
                if (!newFrames.isEmpty()) {
                    QMetaObject::invokeMethod(this, [this, newFrames, loadEnd]() {
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
                        m_isLoadingCanData = false;
                        
                        emit canLogLoaded("", m_canFrames.size(), 0);
                    }, Qt::QueuedConnection);
                } else {
                    QMetaObject::invokeMethod(this, [this]() {
                        m_isLoadingCanData = false;
                    }, Qt::QueuedConnection);
                }
                
                return true;
            });
            
            m_canLogNextLoadTimestamp = loadEnd;
        } else if (m_currentCanLogIndex + 1 < m_canLogFileList.size()) {
            m_currentCanLogIndex++;
            QString nextLogFile = m_canLogFileList[m_currentCanLogIndex];
            
            emit statusMessage(tr("自动加载下一个CAN log: %1").arg(QFileInfo(nextLogFile).fileName()));
            
            loadCanLogByTimestamp(m_baseTimestampMs);
        }
    }
}

// ===== 时间戳工具 =====

QString CanDataManager::formatTime(qint64 ms)
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

QString CanDataManager::formatDateTime(qint64 timestampMs)
{
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    return dateTime.toString("yy/MM/dd HH:mm:ss.zzz");
}
