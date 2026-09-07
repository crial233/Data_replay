#ifndef CAN_DATA_MANAGER_H
#define CAN_DATA_MANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QMap>
#include <QPair>
#include <QJsonObject>
#include <QSet>
#include <QDate>
#include <QTimer>

class QTableView;
class SimpleTableModel;

struct CanFrame {
    qint64 originalTimeMs = 0;  // Absolute timestamp from the CAN log.
    qint64 timeMs = 0;          // Relative timestamp after loading.
    quint32 can_id = 0;         // Effective CAN ID.
    qint32 raw_can_id = 0;      // Signed/raw CAN ID from the log.
    quint8 can_dlc = 0;
    QByteArray data;            // Raw CAN payload bytes.
    QVector<double> decodedValues; // Values from "CAN数据解析后" lines.
    bool hasRawData = false;
    bool hasDecodedValues = false;
    QJsonObject payload;
};

class CanDataManager : public QObject
{
    Q_OBJECT

public:
    explicit CanDataManager(QObject *parent = nullptr);

    void setCanTables(QTableView *canTable, QTableView *canRawTable,
                      SimpleTableModel *canTableModel, SimpleTableModel *canRawTableModel);

    void setCanLogFolderPath(const QString &path);
    QString canLogFolderPath() const { return m_canLogFolderPath; }
    void loadCanLogFolder();

    bool loadCanLogByTimestamp(qint64 timestamp, qint64 targetPositionMs = 0);
    void loadNextCanLogIfNeeded(qint64 currentAbsoluteTimestamp);

    void updateCanDisplay(qint64 positionMs);
    QVector<QStringList> rawFrameRowsAround(qint64 positionMs,
                                            qint64 rangeMs = 500,
                                            qint64 nextFileThresholdMs = 5000);

    void clearCanCache();

    bool hasCanData() const { return !m_canFrames.isEmpty(); }
    int canFrameCount() const { return m_canFrames.size(); }
    qint64 firstFrameTimeMs() const { return m_canFrames.isEmpty() ? 0 : m_canFrames.first().timeMs; }
    qint64 lastFrameTimeMs() const { return m_canFrames.isEmpty() ? 0 : m_canFrames.last().timeMs; }

    void setBaseTimestampMs(qint64 ts) { m_baseTimestampMs = ts; }
    qint64 baseTimestampMs() const { return m_baseTimestampMs; }

    void setLastCanUpdateMs(qint64 ms) { m_lastCanUpdateMs = ms; }
    qint64 lastCanUpdateMs() const { return m_lastCanUpdateMs; }

    void setLastSeekPositionMs(qint64 ms) { m_lastSeekPositionMs = ms; }
    qint64 lastSeekPositionMs() const { return m_lastSeekPositionMs; }

    QVector<QString> canLogFileList() const { return m_canLogFileList; }

    static QString formatTime(qint64 ms);
    static QString formatDateTime(qint64 timestampMs);

signals:
    void statusMessage(const QString &message);
    void canLogLoaded(const QString &fileName, int frameCount, qint64 bestDiff);

private:
    void growColumnWidths(QTableView *table, const QVector<QStringList> &rows,
                          QVector<int> &maximumWidths);
    bool parseCanLog(const QString &fileName, QString *error);
    bool parseCanLogLine(const QString &line, CanFrame &frame, QString *error);
    qint64 logLineTimestampMs(const QString &line) const;
    qint64 parseTimeStringMs(const QString &value) const;

    float extractBits(const QByteArray &data, int start_bit, int len, bool is_intel);
    void parseCanSignals(quint32 can_id, const QVector<double> &decodedValues,
                         QMap<QString, QPair<QString, QString>> &signalList);

    QTableView *m_canTable = nullptr;
    QTableView *m_canRawTable = nullptr;
    SimpleTableModel *m_canTableModel = nullptr;
    SimpleTableModel *m_canRawTableModel = nullptr;
    QVector<int> m_canTableMaximumWidths;
    QVector<int> m_canRawTableMaximumWidths;

    QVector<CanFrame> m_canFrames;
    QString m_canLogFolderPath;
    QVector<QString> m_canLogFileList;
    int m_currentCanLogIndex = -1;
    qint64 m_canLogNextLoadTimestamp = 0;
    qint64 m_canLogFilePosition = 0;
    bool m_isLoadingCanData = false;
    quint64 m_canLoadGeneration = 0;
    QSet<quint32> m_rawCanIds;
    QSet<quint32> m_decodedCanIds;

    qint64 m_baseTimestampMs = 0;

    qint64 m_lastCanUpdateMs = -1;
    qint64 m_lastSeekPositionMs = -1;
};

#endif // CAN_DATA_MANAGER_H
