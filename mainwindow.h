#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QAbstractTableModel>
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QDateTime>
#include <QTimer>
#include <QtConcurrent>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QLabel;
class QPushButton;
class QSlider;
class QTableView;
class QTextEdit;
class QComboBox;
class QDateEdit;
class QListWidget;
class QVideoWidget;
class QMediaPlayer;

#if QT_VERSION_MAJOR >= 6
class QAudioOutput;
#endif

// 高性能表格模型：内部用 QVector<QStringList> 存储，一次性刷新视图
class SimpleTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit SimpleTableModel(const QStringList &headers, QObject *parent = nullptr)
        : QAbstractTableModel(parent), m_headers(headers) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return m_rows.size();
    }
    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent);
        return m_headers.size();
    }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !index.isValid()) return QVariant();
        if (index.row() >= m_rows.size()) return QVariant();
        const QStringList &row = m_rows[index.row()];
        if (index.column() >= row.size()) return QVariant();
        return row[index.column()];
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole) return QVariant();
        if (orientation == Qt::Horizontal && section < m_headers.size())
            return m_headers[section];
        return QVariant();
    }

    // 一次性替换所有数据，触发一次重绘
    void setRows(QVector<QStringList> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

private:
    QStringList m_headers;
    QVector<QStringList> m_rows;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    struct CanFrame {
        qint64 timeMs = 0;          // 时间戳（毫秒）
        quint32 can_id = 0;         // CAN ID（无符号32位）
        quint8 can_dlc = 0;         // 数据长度
        QByteArray data;            // CAN数据（最多8字节）
        QJsonObject payload;        // 完整的JSON数据（用于显示）
    };

    void buildUi();
    void connectSignals();
    void openVideoFolder();
    void openCanLogFolder();
    void onDateChanged(const QDate &date);
    void onVideoSelected(int row);
    void togglePlayback();
    void setPlaybackRate(qreal rate);
    void seekFromSlider(int value);
    void updatePosition(qint64 positionMs);
    void updateDuration(qint64 durationMs);
    void updateCanDisplay(qint64 positionMs);
    void updateCanTableByFrame(qint64 positionMs);
    void loadVideoList(const QDate &date);
    void loadCanLogFolder();
    void loadNextCanLogIfNeeded(qint64 currentAbsoluteTimestamp);
    bool loadCanLogByTimestamp(qint64 timestamp, qint64 targetPositionMs = 0);
    void updateCanLogFileList();
    float extractBits(const QByteArray &data, int start_bit, int len, bool is_intel);
    void parseCanSignals(quint32 can_id, const QByteArray &data, QMap<QString, QPair<QString, QString>> &signalList);
    void setPlaybackUi(bool playing);
    void setStatus(const QString &message);
    void clearCanCache();  // 清除CAN数据缓存
    
    QDate dateFromTimestamp(qint64 timestampMs) const;
    qint64 timestampFromDate(const QDate &date) const;

    bool parseCanLog(const QString &fileName, QString *error);
    bool parseCanLogLine(const QString &line, CanFrame &frame, QString *error);
    qint64 parseTimeStringMs(const QString &value) const;
    QString formatTime(qint64 ms) const;
    QString formatDateTime(qint64 timestampMs) const;

    Ui::MainWindow *ui;
    QMediaPlayer *m_player = nullptr;
#if QT_VERSION_MAJOR >= 6
    QAudioOutput *m_audioOutput = nullptr;
#endif
    QVideoWidget *m_videoWidget = nullptr;
    QPushButton *m_openVideoFolderButton = nullptr;
    QPushButton *m_openCanLogFolderButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QComboBox *m_speedComboBox = nullptr;  // 倍速选择器
    QPushButton *m_dateButton = nullptr;  // 日期选择按钮
    QListWidget *m_videoListWidget = nullptr;  // 视频列表
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_timestampLabel = nullptr;  // 显示时间戳绝对值
    QLabel *m_canStatusLabel = nullptr;
    QTableView *m_canTable = nullptr;           // 上面的表格：信号解析
    QTableView *m_canRawTable = nullptr;        // 下面的表格：PCAN-View风格
    SimpleTableModel *m_canTableModel = nullptr;    // 信号表模型
    SimpleTableModel *m_canRawTableModel = nullptr; // PCAN-View表模型
    QVector<CanFrame> m_canFrames;
    qint64 m_durationMs = 0;
    qint64 m_baseTimestampMs = 0;  // 视频的基础时间戳（毫秒）
    bool m_sliderPressed = false;
    
    QString m_videoFolderPath;  // 视频文件夹路径
    QString m_canLogFolderPath;  // CAN log文件夹路径
    QDate m_currentDate;  // 当前选择的日期
    QVector<QString> m_videoFileList;  // 视频文件列表
    QVector<QString> m_canLogFileList;  // CAN log文件列表
    int m_currentCanLogIndex = -1;  // 当前CAN log索引
    QTimer *m_canUpdateTimer = nullptr;  // CAN显示更新定时器
    qint64 m_canLogNextLoadTimestamp = 0;  // 下次加载CAN数据的时间戳（绝对时间）
    qint64 m_canLogFilePosition = 0;  // CAN log文件读取位置
    bool m_isLoadingCanData = false;  // 防止重复加载的标志
    qint64 m_lastSeekPositionMs = -1;  // 最近一次拖动的目标位置
    QTimer *m_seekDebounceTimer = nullptr;  // 拖动后防抖定时器
    qint64 m_lastCanUpdateMs = -1;  // 上次CAN显示更新的位置（替代静态变量）
};
#endif // MAINWINDOW_H
