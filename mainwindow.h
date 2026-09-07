#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDate>
#include <QTimer>

#include "simple_table_model.h"
#include "can_data_manager.h"
#include "video_player_manager.h"
#include "audio_player_manager.h"
#include "replay_clock.h"
#include "replay_timeline_widget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QLabel;
class QPushButton;
class QTableView;
class QTreeWidget;
class QComboBox;
class QCalendarWidget;
class QListWidget;
class QSplitter;
class QDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void connectSignals();
    void openVideoFolder();
    void openAudioFolder();
    void openCanLogFolder();
    void onDateChanged(const QDate &date);
    void togglePlayback();
    void setPlaybackRate(qreal rate);
    void seekOnTimeline(qint64 positionMs);
    void updatePosition(qint64 positionMs);
    void updateDuration(qint64 durationMs);
    void updateFromReplayClock();
    void showRawCanFramesDialog();
    void refreshRawCanFramesDialog(qint64 positionMs);
    void growRawCanFramesColumnWidths(const QVector<QStringList> &rows);
    void setPlaybackUi(bool playing);
    void setStatus(const QString &message);
    void loadVideoList(const QDate &date);
    void initializeDefaultFolders();

    QDate dateFromTimestamp(qint64 timestampMs) const;
    qint64 timestampFromDate(const QDate &date) const;
    QString defaultVideoFolderPath() const;
    QString defaultAudioFolderPath() const;
    QString defaultCanLogFolderPath() const;

    Ui::MainWindow *ui;

    // 子模块
    VideoPlayerManager *m_videoManager = nullptr;
    AudioPlayerManager *m_audioManager = nullptr;
    CanDataManager *m_canManager = nullptr;

    // UI组件
    QPushButton *m_openVideoFolderButton = nullptr;
    QPushButton *m_openAudioFolderButton = nullptr;
    QPushButton *m_openCanLogFolderButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_canSidebarEdgeButton = nullptr;
    QListWidget *m_videoListWidget = nullptr;
    QLabel *m_videoFolderLabel = nullptr;
    QLabel *m_audioFolderLabel = nullptr;
    QComboBox *m_speedComboBox = nullptr;
    QPushButton *m_rewind30Button = nullptr;
    QPushButton *m_rewind15Button = nullptr;
    QPushButton *m_rewind5Button = nullptr;
    QPushButton *m_rewind1Button = nullptr;
    QPushButton *m_forward1Button = nullptr;
    QPushButton *m_forward5Button = nullptr;
    QPushButton *m_forward15Button = nullptr;
    QPushButton *m_forward30Button = nullptr;
    QCalendarWidget *m_calendarWidget = nullptr;
    ReplayTimelineWidget *m_timelineWidget = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_timestampLabel = nullptr;
    QLabel *m_canStatusLabel = nullptr;
    QPushButton *m_showRawCanFramesButton = nullptr;
    QTableView *m_canTable = nullptr;
    QTableView *m_canRawTable = nullptr;
    QDialog *m_rawCanFramesDialog = nullptr;
    QLabel *m_rawCanFramesInfoLabel = nullptr;
    QTreeWidget *m_rawCanFramesTable = nullptr;
    QComboBox *m_rawCanIdFilter = nullptr;
    QVector<int> m_rawCanFramesMaximumWidths;
    qint64 m_lastRawCanFramesDialogPositionMs = -1;
    QWidget *m_canSidebarWidget = nullptr;
    QWidget *m_canSidebarToggleStrip = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    SimpleTableModel *m_canTableModel = nullptr;
    SimpleTableModel *m_canRawTableModel = nullptr;

    // 状态
    qint64 m_durationMs = 0;
    qint64 m_baseTimestampMs = 0;
    ReplayClock m_replayClock;
    QDate m_currentDate;
    QTimer *m_canUpdateTimer = nullptr;
    QTimer *m_clockUpdateTimer = nullptr;
    QTimer *m_seekDebounceTimer = nullptr;
};
#endif // MAINWINDOW_H
