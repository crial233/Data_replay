#include "can_data_manager.h"
#include "can_decoded_signal_mapping.h"
#include "simple_table_model.h"
#include "video_player_manager.h"
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
#include <QFontMetrics>

#include <algorithm>

namespace {

QString fieldValue(const QString &line, const QString &key)
{
    const int keyPos = line.indexOf(key);
    if (keyPos < 0) {
        return {};
    }

    const int valueStart = keyPos + key.size();
    int valueEnd = line.indexOf(',', valueStart);
    if (valueEnd < 0) {
        valueEnd = line.size();
    }
    return line.mid(valueStart, valueEnd - valueStart).trimmed();
}

bool parseEffectiveCanId(const QString &text, quint32 &canId)
{
    const QString trimmed = text.trimmed();
    bool ok = false;
    if (trimmed.startsWith("0x", Qt::CaseInsensitive)) {
        canId = trimmed.toUInt(&ok, 16) & 0x1FFFFFFF;
        return ok;
    }

    const int signedId = trimmed.toInt(&ok, 10);
    if (!ok) {
        return false;
    }

    canId = static_cast<quint32>(signedId) & 0x1FFFFFFF;
    return true;
}

bool parseRawByte(const QString &text, int &value)
{
    QString trimmed = text.trimmed();
    if (trimmed.startsWith("0x", Qt::CaseInsensitive)) {
        trimmed.remove(0, 2);
    }
    bool ok = false;
    value = trimmed.toInt(&ok, 16);
    return ok && value >= 0 && value <= 0xFF;
}

QString formatRawDataHex(const QByteArray &data)
{
    QString dataHex;
    dataHex.reserve(data.size() * 3);
    for (int i = 0; i < data.size() && i < 8; ++i) {
        if (i > 0) dataHex += ' ';
        dataHex += QString("%1").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0'));
    }
    return dataHex;
}

} // namespace

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

    m_canTableMaximumWidths.clear();
    m_canRawTableMaximumWidths.clear();
    if (m_canTable && m_canTableModel) {
        for (int column = 0; column < m_canTableModel->columnCount(); ++column) {
            m_canTableMaximumWidths.append(m_canTable->columnWidth(column));
        }
    }
    if (m_canRawTable && m_canRawTableModel) {
        for (int column = 0; column < m_canRawTableModel->columnCount(); ++column) {
            m_canRawTableMaximumWidths.append(m_canRawTable->columnWidth(column));
        }
    }
}

void CanDataManager::growColumnWidths(QTableView *table,
                                      const QVector<QStringList> &rows,
                                      QVector<int> &maximumWidths)
{
    if (!table || rows.isEmpty()) return;

    const QFontMetrics metrics(table->font());
    for (const QStringList &row : rows) {
        if (maximumWidths.size() < row.size()) {
            maximumWidths.resize(row.size());
        }
        for (int column = 0; column < row.size(); ++column) {
            // 留出单元格左右边距；宽度只在出现更长数据时增长。
            const int requiredWidth = metrics.horizontalAdvance(row[column]) + 24;
            if (requiredWidth <= maximumWidths[column]) continue;

            maximumWidths[column] = requiredWidth;
            table->setColumnWidth(column, qMax(table->columnWidth(column), requiredWidth));
        }
    }
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
    const int bracketStart = line.indexOf('[');
    const int bracketEnd = line.lastIndexOf(']');

    if (bracketStart == -1 || bracketEnd == -1 || bracketEnd <= bracketStart) {
        if (error) *error = tr("No data array");
        return false;
    }

    const QString dataStr = line.mid(bracketStart + 1, bracketEnd - bracketStart - 1);
    const QStringList dataValues = dataStr.split(',', Qt::SkipEmptyParts);

    if (line.contains("time=") && line.contains("can_id=") && line.contains("data=[")) {
        bool ok = false;
        const qint64 timestamp = fieldValue(line, "time=").toLongLong(&ok);
        if (!ok) {
            if (error) *error = tr("Decoded timestamp parse failed");
            return false;
        }

        quint32 canId = 0;
        if (!parseEffectiveCanId(fieldValue(line, "can_id="), canId)) {
            if (error) *error = tr("Decoded CAN ID parse failed");
            return false;
        }

        qint32 rawCanId = 0;
        const QString rawCanIdText = fieldValue(line, "raw_can_id=");
        if (!rawCanIdText.isEmpty()) {
            rawCanId = rawCanIdText.toInt(&ok, 10);
            if (!ok) rawCanId = 0;
        }

        frame.originalTimeMs = timestamp;
        frame.timeMs = timestamp;
        frame.can_id = canId;
        frame.raw_can_id = rawCanId;
        frame.hasDecodedValues = true;

        for (const QString &valueText : dataValues) {
            const double value = valueText.trimmed().toDouble(&ok);
            if (ok) {
                frame.decodedValues.append(value);
            }
        }

        QJsonArray decodedArray;
        for (double value : frame.decodedValues) {
            decodedArray.append(value);
        }

        QJsonObject jsonObj;
        jsonObj.insert("can_id", static_cast<qint64>(frame.can_id));
        jsonObj.insert("raw_can_id", frame.raw_can_id);
        jsonObj.insert("decoded_data", decodedArray);
        frame.payload = jsonObj;
        return !frame.decodedValues.isEmpty();
    }

    const QString headerPart = line.left(bracketStart).trimmed();
    const QStringList headerParts = headerPart.split(',', Qt::KeepEmptyParts);
    if (headerParts.size() < 2) {
        if (error) *error = tr("Raw CAN line header parse failed");
        return false;
    }

    bool ok = false;
    const qint64 timestamp = headerParts[0].trimmed().toLongLong(&ok);
    if (!ok) {
        if (error) *error = tr("Raw timestamp parse failed");
        return false;
    }

    quint32 canId = 0;
    if (!parseEffectiveCanId(headerParts[1], canId)) {
        if (error) *error = tr("Raw CAN ID parse failed");
        return false;
    }

    qint32 rawCanId = 0;
    if (headerParts.size() >= 3) {
        rawCanId = headerParts[2].trimmed().toInt(&ok, 10);
        if (!ok) rawCanId = 0;
    }

    frame.originalTimeMs = timestamp;
    frame.timeMs = timestamp;
    frame.can_id = canId;
    frame.raw_can_id = rawCanId;
    frame.hasRawData = true;

    for (int i = 0; i < dataValues.size() && i < 8; ++i) {
        int byteValue = 0;
        if (parseRawByte(dataValues[i], byteValue)) {
            frame.data.append(static_cast<char>(byteValue & 0xFF));
        }
    }

    frame.can_dlc = static_cast<quint8>(frame.data.size());

    QJsonArray dataArray;
    for (int i = 0; i < frame.data.size(); ++i) {
        dataArray.append(static_cast<int>(static_cast<unsigned char>(frame.data[i])));
    }

    QJsonObject jsonObj;
    jsonObj.insert("can_id", static_cast<qint64>(frame.can_id));
    jsonObj.insert("raw_can_id", frame.raw_can_id);
    jsonObj.insert("can_dlc", frame.can_dlc);
    jsonObj.insert("data", dataArray);
    frame.payload = jsonObj;
    return frame.can_dlc > 0;
}

qint64 CanDataManager::logLineTimestampMs(const QString &line) const
{
    if (line.contains("time=") && line.contains("can_id=") && line.contains("data=[")) {
        bool ok = false;
        const qint64 timestamp = fieldValue(line, "time=").toLongLong(&ok);
        return ok ? timestamp : -1;
    }

    const int commaPos = line.indexOf(',');
    if (commaPos < 0) {
        return -1;
    }

    bool ok = false;
    const qint64 timestamp = line.left(commaPos).trimmed().toLongLong(&ok);
    return ok ? timestamp : -1;
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

void CanDataManager::parseCanSignals(quint32 can_id, const QVector<double> &decodedValues,
                                     QMap<QString, QPair<QString, QString>> &signalList)
{
    for (int i = 0; i < decodedValues.size(); ++i) {
        const CanDecodedSignalDef *def = findCanDecodedSignalDef(can_id, i);
        if (!def) {
            continue;
        }

        const QString signalName = QString::fromUtf8(def->displayNameZh);
        const QString variableName = QString::fromUtf8(def->variableName);
        const QString unit = QString::fromUtf8(def->unit);
        const QString valueStr = QString("%1 (%2)")
            .arg(decodedValues[i], 0, 'f', 6)
            .arg(variableName);

        signalList[signalName] = qMakePair(valueStr, unit);
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

    const auto it = std::upper_bound(m_canFrames.cbegin(), m_canFrames.cend(), positionMs,
                                     [](qint64 position, const CanFrame &frame) {
                                         return position < frame.timeMs;
                                     });

    const int frameCount = std::distance(m_canFrames.cbegin(), it);
    if (frameCount == 0) {
        if (m_canRawTableModel) m_canRawTableModel->setRows({});
        if (m_canTableModel) m_canTableModel->setRows({});
        emit statusMessage(tr("CAN 数据：无"));
        return;
    }

    QMap<quint32, const CanFrame*> latestRawFrames;
    QMap<quint32, const CanFrame*> latestDecodedFrames;
    // Walk backwards until the latest value for every known ID is found. A fixed
    // 500-frame window dropped low-frequency IDs and made the table look misaligned.
    auto frameIt = it;
    while (frameIt != m_canFrames.cbegin() &&
           (latestRawFrames.size() < m_rawCanIds.size() ||
            latestDecodedFrames.size() < m_decodedCanIds.size())) {
        --frameIt;
        if (frameIt->hasRawData) {
            if (!latestRawFrames.contains(frameIt->can_id))
                latestRawFrames[frameIt->can_id] = &(*frameIt);
        }
        if (frameIt->hasDecodedValues) {
            if (!latestDecodedFrames.contains(frameIt->can_id))
                latestDecodedFrames[frameIt->can_id] = &(*frameIt);
        }
    }

    const int currentUniqueCanIdCount = latestRawFrames.size() + latestDecodedFrames.size();
    const qint64 t1 = perfTimer.elapsed();

    QVector<QStringList> rawRows;
    rawRows.reserve(latestRawFrames.size());
    for (auto mapIt = latestRawFrames.begin(); mapIt != latestRawFrames.end(); ++mapIt) {
        const CanFrame *frame = mapIt.value();
        rawRows.append({
            QString::number(frame->originalTimeMs),
            QString("0x%1").arg(frame->can_id, 0, 16),
            formatRawDataHex(frame->data)
        });
    }

    struct SignalDisplay {
        qint64 timeMs = 0;
        QString value;
        QString unit;
    };

    QMap<QString, SignalDisplay> allSignals;
    for (auto mapIt = latestDecodedFrames.begin(); mapIt != latestDecodedFrames.end(); ++mapIt) {
        const CanFrame *frame = mapIt.value();
        QMap<QString, QPair<QString, QString>> frameSignals;
        parseCanSignals(frame->can_id, frame->decodedValues, frameSignals);
        for (auto sigIt = frameSignals.begin(); sigIt != frameSignals.end(); ++sigIt) {
            allSignals[sigIt.key()] = SignalDisplay{frame->timeMs, sigIt.value().first, sigIt.value().second};
        }
    }

    const qint64 t2 = perfTimer.elapsed();

    QVector<QStringList> signalRows;
    signalRows.reserve(allSignals.size());
    for (auto sigIt = allSignals.begin(); sigIt != allSignals.end(); ++sigIt) {
        signalRows.append({
            formatDateTime(m_baseTimestampMs + sigIt.value().timeMs),
            sigIt.key(),
            sigIt.value().value,
            sigIt.value().unit
        });
    }

    const qint64 t3 = perfTimer.elapsed();

    growColumnWidths(m_canRawTable, rawRows, m_canRawTableMaximumWidths);
    growColumnWidths(m_canTable, signalRows, m_canTableMaximumWidths);
    if (m_canRawTableModel) m_canRawTableModel->setRows(rawRows);
    if (m_canTableModel) m_canTableModel->setRows(signalRows);

    const qint64 t4 = perfTimer.elapsed();

    if (rawRows.isEmpty() || signalRows.isEmpty()) {
        qDebug() << "[updateCanDisplay] empty table check"
                 << "positionMs:" << positionMs
                 << "frameCount:" << frameCount
                 << "latestRawIds:" << latestRawFrames.size()
                 << "latestDecodedIds:" << latestDecodedFrames.size()
                 << "rawRows:" << rawRows.size()
                 << "signalRows:" << signalRows.size();
    }

    if (!m_canFrames.isEmpty()) {
        const CanFrame &lastFrame = *(it - 1);
        const qint64 originalAbsoluteTimestamp = lastFrame.originalTimeMs > 0
            ? lastFrame.originalTimeMs
            : lastFrame.timeMs + m_baseTimestampMs;

        QString logInfo;
        if (m_currentCanLogIndex >= 0 && m_currentCanLogIndex < m_canLogFileList.size()) {
            logInfo = tr(" | Log: %1/%2")
                .arg(m_currentCanLogIndex + 1)
                .arg(m_canLogFileList.size());
        }

        emit statusMessage(tr("CAN 帧数: %1 | 最后更新: %2%3")
            .arg(rawRows.size())
            .arg(originalAbsoluteTimestamp)
            .arg(logInfo));
    }

    const qint64 elapsed = perfTimer.elapsed();
    if (elapsed > 5) {
        qDebug() << "[updateCanDisplay] 总耗时:" << elapsed << "ms"
                 << "(分组:" << t1 << "构建:" << (t2-t1) << "信号行:" << (t3-t2)
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

    auto makeRows = [this](const CanFrame &frame) {
        QString dataHex;
        dataHex.reserve(frame.data.size() * 3);
        for (int i = 0; i < frame.data.size() && i < 8; ++i) {
            if (i > 0) dataHex += ' ';
            dataHex += QString("%1").arg(static_cast<unsigned char>(frame.data[i]), 2, 16, QChar('0'));
        }

        QMap<QString, QPair<QString, QString>> parsedSignals;
        if (frame.hasDecodedValues) {
            parseCanSignals(frame.can_id, frame.decodedValues, parsedSignals);
        }
        const QStringList commonColumns{
            formatDateTime(m_baseTimestampMs + frame.timeMs),
            QString::number(frame.originalTimeMs),
            QString("0x%1").arg(frame.can_id, 0, 16),
            dataHex
        };

        QVector<QStringList> frameRows;
        if (parsedSignals.isEmpty()) {
            QStringList row = commonColumns;
            row.append(QString());
            frameRows.append(std::move(row));
            return frameRows;
        }

        frameRows.reserve(parsedSignals.size());
        for (auto sigIt = parsedSignals.begin(); sigIt != parsedSignals.end(); ++sigIt) {
            QStringList row = commonColumns;
            row.append(QString("%1=%2 %3")
                .arg(sigIt.key())
                .arg(sigIt.value().first)
                .arg(sigIt.value().second));
            frameRows.append(std::move(row));
        }
        return frameRows;
    };

    auto lowerIt = std::lower_bound(frames.cbegin(), frames.cend(), startTimeMs,
                                    [](const CanFrame &frame, qint64 position) {
                                        return frame.timeMs < position;
                                    });
    if (frames.last().timeMs < endTimeMs && m_currentCanLogIndex + 1 < m_canLogFileList.size()) {
        const QString nextLogFile = m_canLogFileList[m_currentCanLogIndex + 1];
        bool ok = false;
        const qint64 nextFileTimestamp = VideoPlayerManager::timestampFromVideoFileName(QFileInfo(nextLogFile).baseName(), &ok);
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

                    const qint64 lineTimestamp = logLineTimestampMs(line);
                    if (lineTimestamp < 0 || lineTimestamp < startOriginalTime) {
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
        const QVector<QStringList> frameRows = makeRows(*it);
        rows += frameRows;
    }

    return rows;
}

// ===== CAN缓存清除 =====

void CanDataManager::clearCanCache()
{
    ++m_canLoadGeneration;
    m_canFrames.clear();
    m_rawCanIds.clear();
    m_decodedCanIds.clear();
    
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
        bool ok = false;
	VideoPlayerManager::timestampFromVideoFileName(file.baseName(), &ok);
	if (ok) {
    	    m_canLogFileList.append(file.absoluteFilePath());
	}	
    }
    
    // 按时间戳排序
    std::sort(m_canLogFileList.begin(), m_canLogFileList.end(), 
        [](const QString &a, const QString &b) {
            bool okA = false, okB = false;
            qint64 ta = VideoPlayerManager::timestampFromVideoFileName(QFileInfo(a).baseName(), &okA);
            qint64 tb = VideoPlayerManager::timestampFromVideoFileName(QFileInfo(b).baseName(), &okB);
            return ta < tb;
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
        bool ok = false;
	qint64 logTimestamp = VideoPlayerManager::timestampFromVideoFileName(QFileInfo(m_canLogFileList[i]).baseName(), &ok);
	if (!ok) continue;  // 若解析失败则跳过该文件
        
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
    const quint64 generation = ++m_canLoadGeneration;
    const qint64 baseTimestamp = m_baseTimestampMs;

    // Keep enough history to recover the last value of low-frequency CAN IDs.
    const qint64 preloadStart = absoluteTarget - 60000;
    const qint64 preloadEnd = absoluteTarget + 60000;

    auto *watcher = new QFutureWatcher<QVector<CanFrame>>(this);
    connect(watcher, &QFutureWatcher<QVector<CanFrame>>::finished, this,
            [this, watcher, generation, logFile, bestDiff, absoluteTarget,
             preloadEnd, baseTimestamp]() {
        QVector<CanFrame> frames = watcher->result();
        watcher->deleteLater();
        if (generation != m_canLoadGeneration) return;

        std::stable_sort(frames.begin(), frames.end(), [](const CanFrame &a, const CanFrame &b) {
            return a.timeMs < b.timeMs;
        });
        m_canFrames = std::move(frames);
        m_rawCanIds.clear();
        m_decodedCanIds.clear();
        for (const CanFrame &frame : m_canFrames) {
            if (frame.hasRawData) m_rawCanIds.insert(frame.can_id);
            if (frame.hasDecodedValues) m_decodedCanIds.insert(frame.can_id);
        }
        m_canLogNextLoadTimestamp = preloadEnd;
        m_canLogFilePosition = 0;
        m_isLoadingCanData = false;

        if (m_canFrames.isEmpty()) {
            emit statusMessage(tr("CAN log 中没有目标时间附近的数据"));
            return;
        }
        emit statusMessage(tr("已加载CAN log: %1 (差异: %2 ms, %3 帧)")
            .arg(QFileInfo(logFile).fileName()).arg(bestDiff).arg(m_canFrames.size()));
        emit canLogLoaded(QFileInfo(logFile).fileName(), m_canFrames.size(), bestDiff);
        const qint64 displayPos = (m_lastSeekPositionMs >= 0)
            ? m_lastSeekPositionMs
            : qMax<qint64>(0, absoluteTarget - baseTimestamp);
        updateCanDisplay(displayPos);
    });

    watcher->setFuture(QtConcurrent::run([this, logFile, preloadStart, preloadEnd, baseTimestamp]() {
        QFile file(logFile);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QVector<CanFrame>{};
        }
        
        QVector<CanFrame> frames;
        QTextStream in(&file);
        
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) continue;
            
            qint64 lineTimestamp = logLineTimestampMs(line);
            if (lineTimestamp < 0) continue;
            
            if (lineTimestamp >= preloadStart && lineTimestamp <= preloadEnd) {
                CanFrame frame;
                QString lineError;
                if (parseCanLogLine(line, frame, &lineError)) {
                    frame.timeMs = frame.timeMs - baseTimestamp;
                    frames.append(frame);
                }
            }
        }
        return frames;
    }));
    
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
    
    if (timeToEnd < 10000) {
        qDebug() << "[loadNextCanLogIfNeeded] 接近末尾，加载后续数据, timeToEnd:" << timeToEnd;
        
        if (m_currentCanLogIndex < m_canLogFileList.size()) {
            QString currentLogFile = m_canLogFileList[m_currentCanLogIndex];
            
            qint64 loadStart = m_canLogNextLoadTimestamp;
            qint64 loadEnd = loadStart + 60000;
            const quint64 generation = m_canLoadGeneration;
            
            qDebug() << "[loadNextCanLogIfNeeded] 加载后续数据:" << loadStart << "~" << loadEnd;
            
            m_isLoadingCanData = true;
            
            QFuture<bool> future = QtConcurrent::run([this, currentLogFile, loadStart, loadEnd, generation]() {
                QFile file(currentLogFile);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    return false;
                }
                
                QVector<CanFrame> newFrames;
                QTextStream in(&file);
                
                while (!in.atEnd()) {
                    QString line = in.readLine().trimmed();
                    if (line.isEmpty()) continue;
                    
                    qint64 lineTimestamp = logLineTimestampMs(line);
                    if (lineTimestamp < 0) continue;
                    
                    if (lineTimestamp < loadStart) {
                        continue;
                    }
                    
                    if (lineTimestamp > loadStart && lineTimestamp <= loadEnd) {
                        CanFrame frame;
                        QString lineError;
                        if (parseCanLogLine(line, frame, &lineError)) {
                            frame.timeMs = frame.timeMs - m_baseTimestampMs;
                            newFrames.append(frame);
                        }
                    }
                    
                }
                
                if (!newFrames.isEmpty()) {
                    QMetaObject::invokeMethod(this, [this, newFrames, loadEnd, generation]() {
                        if (generation != m_canLoadGeneration) return;
                        QVector<CanFrame> sortedFrames = newFrames;
                        std::stable_sort(sortedFrames.begin(), sortedFrames.end(),
                                         [](const CanFrame &a, const CanFrame &b) {
                            return a.timeMs < b.timeMs;
                        });
                        QVector<CanFrame> merged;
                        merged.reserve(m_canFrames.size() + sortedFrames.size());
                        std::merge(m_canFrames.cbegin(), m_canFrames.cend(),
                                   sortedFrames.cbegin(), sortedFrames.cend(),
                                   std::back_inserter(merged),
                                   [](const CanFrame &a, const CanFrame &b) {
                                       return a.timeMs < b.timeMs;
                                   });
                        m_canFrames = std::move(merged);
                        for (const CanFrame &frame : sortedFrames) {
                            if (frame.hasRawData) m_rawCanIds.insert(frame.can_id);
                            if (frame.hasDecodedValues) m_decodedCanIds.insert(frame.can_id);
                        }
                        m_canLogNextLoadTimestamp = loadEnd;
                        m_isLoadingCanData = false;
                        
                        emit canLogLoaded("", m_canFrames.size(), 0);
                    }, Qt::QueuedConnection);
                } else {
                    QMetaObject::invokeMethod(this, [this, loadEnd, generation]() {
                        if (generation != m_canLoadGeneration) return;
                        m_canLogNextLoadTimestamp = loadEnd;
                        if (m_currentCanLogIndex + 1 < m_canLogFileList.size()) {
                            ++m_currentCanLogIndex;
                        }
                        m_isLoadingCanData = false;
                    }, Qt::QueuedConnection);
                }
                
                return true;
            });
            
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
    return dateTime.toString("yy/MM/dd | HH:mm:ss.zzz");
}



