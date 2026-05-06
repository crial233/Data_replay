#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDate>
#include <QTimer>

#include "simple_table_model.h"
#include "can_data_manager.h"
#include "video_player_manager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QLabel;
class QPushButton;
class QSlider;
class QTableView;
class QComboBox;
class QCalendarWidget;
class QListWidget;

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
    void openCanLogFolder();
    void onDateChanged(const QDate &date);
    void togglePlayback();
    void setPlaybackRate(qreal rate);
    void seekFromSlider(int value);
    void updatePosition(qint64 positionMs);
    void updateDuration(qint64 durationMs);
    void setPlaybackUi(bool playing);
    void setStatus(const QString &message);
    void loadVideoList(const QDate &date);

    QDate dateFromTimestamp(qint64 timestampMs) const;
    qint64 timestampFromDate(const QDate &date) const;

    Ui::MainWindow *ui;

    // 子模块
    VideoPlayerManager *m_videoManager = nullptr;
    CanDataManager *m_canManager = nullptr;

    // UI组件
    QPushButton *m_openVideoFolderButton = nullptr;
    QPushButton *m_openCanLogFolderButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QListWidget *m_videoListWidget = nullptr;
    QLabel *m_videoFolderLabel = nullptr;
    QComboBox *m_speedComboBox = nullptr;
    QPushButton *m_view1Button = nullptr;
    QPushButton *m_view4Button = nullptr;
    QPushButton *m_view8Button = nullptr;
    QCalendarWidget *m_calendarWidget = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_timestampLabel = nullptr;
    QLabel *m_canStatusLabel = nullptr;
    QTableView *m_canTable = nullptr;
    QTableView *m_canRawTable = nullptr;
    SimpleTableModel *m_canTableModel = nullptr;
    SimpleTableModel *m_canRawTableModel = nullptr;

    // 状态
    qint64 m_durationMs = 0;
    qint64 m_baseTimestampMs = 0;
    bool m_sliderPressed = false;
    QDate m_currentDate;
    QTimer *m_canUpdateTimer = nullptr;
    QTimer *m_seekDebounceTimer = nullptr;
};
#endif // MAINWINDOW_H
