#ifndef CAN_DATA_MANAGER_H
#define CAN_DATA_MANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QJsonObject>
#include <QDate>
#include <QTimer>

class QTableView;
class SimpleTableModel;

// CAN数据帧结构
struct CanFrame {
    qint64 timeMs = 0;          // 时间戳（毫秒）
    quint32 can_id = 0;         // CAN ID（无符号32位）
    quint8 can_dlc = 0;         // 数据长度
    QByteArray data;            // CAN数据（最多8字节）
    QJsonObject payload;        // 完整的JSON数据（用于显示）
};

class CanDataManager : public QObject
{
    Q_OBJECT

public:
    explicit CanDataManager(QObject *parent = nullptr);

    // 设置UI组件（由MainWindow创建后注入）
    void setCanTables(QTableView *canTable, QTableView *canRawTable,
                      SimpleTableModel *canTableModel, SimpleTableModel *canRawTableModel);

    // CAN log文件夹管理
    void setCanLogFolderPath(const QString &path);
    QString canLogFolderPath() const { return m_canLogFolderPath; }
    void loadCanLogFolder();

    // CAN log加载
    bool loadCanLogByTimestamp(qint64 timestamp, qint64 targetPositionMs = 0);
    void loadNextCanLogIfNeeded(qint64 currentAbsoluteTimestamp);

    // CAN显示更新
    void updateCanDisplay(qint64 positionMs);

    // CAN缓存清除
    void clearCanCache();

    // CAN数据状态查询
    bool hasCanData() const { return !m_canFrames.isEmpty(); }
    int canFrameCount() const { return m_canFrames.size(); }
    qint64 firstFrameTimeMs() const { return m_canFrames.isEmpty() ? 0 : m_canFrames.first().timeMs; }
    qint64 lastFrameTimeMs() const { return m_canFrames.isEmpty() ? 0 : m_canFrames.last().timeMs; }

    // 基准时间戳
    void setBaseTimestampMs(qint64 ts) { m_baseTimestampMs = ts; }
    qint64 baseTimestampMs() const { return m_baseTimestampMs; }

    // CAN更新节流
    void setLastCanUpdateMs(qint64 ms) { m_lastCanUpdateMs = ms; }
    qint64 lastCanUpdateMs() const { return m_lastCanUpdateMs; }

    // 最近拖动位置
    void setLastSeekPositionMs(qint64 ms) { m_lastSeekPositionMs = ms; }
    qint64 lastSeekPositionMs() const { return m_lastSeekPositionMs; }

    // CAN log文件列表
    QVector<QString> canLogFileList() const { return m_canLogFileList; }

    // 时间戳工具
    static QString formatTime(qint64 ms);
    static QString formatDateTime(qint64 timestampMs);

signals:
    void statusMessage(const QString &message);
    void canLogLoaded(const QString &fileName, int frameCount, qint64 bestDiff);

private:
    // CAN log解析
    bool parseCanLog(const QString &fileName, QString *error);
    bool parseCanLogLine(const QString &line, CanFrame &frame, QString *error);
    qint64 parseTimeStringMs(const QString &value) const;

    // CAN信号解析
    float extractBits(const QByteArray &data, int start_bit, int len, bool is_intel);
    void parseCanSignals(quint32 can_id, const QByteArray &data,
                         QMap<QString, QPair<QString, QString>> &signalList);

    // UI组件指针（由MainWindow注入）
    QTableView *m_canTable = nullptr;
    QTableView *m_canRawTable = nullptr;
    SimpleTableModel *m_canTableModel = nullptr;
    SimpleTableModel *m_canRawTableModel = nullptr;

    // CAN数据
    QVector<CanFrame> m_canFrames;
    QString m_canLogFolderPath;
    QVector<QString> m_canLogFileList;
    int m_currentCanLogIndex = -1;
    qint64 m_canLogNextLoadTimestamp = 0;
    qint64 m_canLogFilePosition = 0;
    bool m_isLoadingCanData = false;

    // 时间基准
    qint64 m_baseTimestampMs = 0;

    // CAN显示更新节流
    qint64 m_lastCanUpdateMs = -1;
    qint64 m_lastSeekPositionMs = -1;
};

#endif // CAN_DATA_MANAGER_H
