#pragma once

#include <QMainWindow>
#include <QVector>
#include <QGridLayout>
#include <QLabel>
#include <QTimer>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QButtonGroup>
#include <QListWidget>
#include <vlc/vlc.h>
#include "ConnectionDialog.h"

class VlcWidget;
class StatusBar;

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(libvlc_instance_t *vlcInstance, QWidget *parent = nullptr);
    ~MainWindow();

    void appendLog(const QString &message, LogLevel level = LogLevel::INFO);

private slots:
    void addChannel();
    void removeSelectedChannel();
    void setGridColumns(int cols);
    void setFileType(int type);
    void refreshFilesList();

private:
    libvlc_instance_t *m_vlcInstance = nullptr;
    QVector<VlcWidget *> m_viewers;
    QVector<QLabel *> m_emptyLabels;
    QWidget *m_gridWidget = nullptr;
    QGridLayout *m_grid = nullptr;
    QTimer *m_clockTimer = nullptr;
    QTableWidget *m_channelTable = nullptr;
    int m_gridCols = 0;

    // 우측 패널
    QTabWidget *m_rightTabs = nullptr;
    QTextEdit *m_logView = nullptr;
    QButtonGroup *m_colBtnGroup = nullptr;

    // 파일 탭 (스냅샷/녹화 목록)
    QListWidget *m_filesList = nullptr;
    QButtonGroup *m_fileTypeGroup = nullptr;
    int m_currentFileType = 0; // 0 = snapshots, 1 = recordings

    // 영상 영역
    QTabWidget *m_videoTabs = nullptr;
    QWidget *m_gridPage = nullptr;

    // 상태 바
    StatusBar *m_statusBar = nullptr;

    // 헬퍼
    VlcWidget *createViewer(const QString &name, const QString &url, bool autoReconnect = true);
    void addChannelToTable(const QString &name, const QString &url);
    int effectiveGridCols() const;

    void rebuildGrid();
    void updateGridCellSizes();
    bool eventFilter(QObject *obj, QEvent *event) override;
    void setupSidebar(QWidget *parent);
    void setupRightPanel(QWidget *parent);
    void setupFilesTab(QWidget *parent);
    void setupVideoArea(QWidget *parent);
    static QString snapshotsDir();
    static QString recordingsDir();
    QString currentFilesDir() const;
    void showFilesContextMenu(const QPoint &pos);
    void openFilesListItem(QListWidgetItem *item);
    void openFullscreenTab(VlcWidget *viewer);
    void closeVideoTab(int index);

    void saveChannels();
    void loadChannels();
    QString channelsFilePath() const;
};
