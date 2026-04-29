#pragma once

#include <QMainWindow>
#include <QVector>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QPoint>
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
class QScrollArea;
class QPushButton;
class QFrame;

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
    QVector<QFrame *> m_gridCells;
    QHash<VlcWidget *, int> m_gridIndexes;
    QWidget *m_gridWidget = nullptr;
    QGridLayout *m_grid = nullptr;
    QTimer *m_clockTimer = nullptr;
    QTimer *m_saveDebounceTimer = nullptr;
    QTableWidget *m_channelTable = nullptr;
    int m_gridCols = 3;

    // 좌우 패널
    QWidget *m_sidebar = nullptr;
    QWidget *m_rightPanel = nullptr;
    QPushButton *m_leftPanelToggle = nullptr;
    QPushButton *m_rightPanelToggle = nullptr;
    bool m_leftPanelVisible = true;
    bool m_rightPanelVisible = true;
    QPoint m_channelDragStartPos;
    int m_channelDragRow = -1;
    bool m_channelDragStarted = false;
    int m_lastChannelClickedRow = -1;
    QPoint m_viewerDragStartPos;
    VlcWidget *m_viewerDragSource = nullptr;
    bool m_viewerDragStarted = false;

    // 우측 패널
    QTabWidget *m_rightTabs = nullptr;
    QTextEdit *m_logView = nullptr;
    QButtonGroup *m_colBtnGroup = nullptr;

    // 녹화 탭 (스냅샷/녹화 목록)
    QListWidget *m_filesList = nullptr;
    QButtonGroup *m_fileTypeGroup = nullptr;
    int m_currentFileType = 0; // 0 = snapshots, 1 = recordings

    // 영상 영역
    QTabWidget *m_videoTabs = nullptr;
    QWidget *m_gridPage = nullptr;
    QScrollArea *m_gridScrollArea = nullptr;
    int m_dropHighlightIndex = -1;

    // 상태 바
    StatusBar *m_statusBar = nullptr;

    // 헬퍼
    VlcWidget *createViewer(const QString &name, const QString &url, bool autoReconnect = true);
    void addChannelToTable(const QString &name, const QString &url);
    int effectiveGridCols() const;

    void rebuildGrid();
    void updateGridCellSizes();
    QPushButton *createPanelToggleButton(QWidget *parent);
    void setLeftPanelVisible(bool visible);
    void setRightPanelVisible(bool visible);
    void updatePanelToggleButtons();
    void editChannel(int row);
    void updateChannelTableRow(int row, const QString &name, const QString &url);
    int firstFreeGridIndex(VlcWidget *except = nullptr) const;
    int maxAssignedGridIndex() const;
    VlcWidget *viewerAtGridIndex(int index, VlcWidget *except = nullptr) const;
    void moveViewerToGridIndex(VlcWidget *viewer, int targetIndex);
    bool startChannelDragIfNeeded(QMouseEvent *event);
    bool startViewerDragIfNeeded(QMouseEvent *event);
    VlcWidget *viewerForDragObject(QObject *obj) const;
    void selectChannelRowFromClick(int row, Qt::KeyboardModifiers modifiers);
    bool handleGridDragEvent(QObject *obj, QEvent *event);
    int gridIndexForDropTarget(QObject *obj, const QPoint &pos) const;
    QFrame *createGridCell();
    void ensureGridCellCount(int count);
    void clearGridCell(QFrame *cell);
    void setGridCellHighlighted(int index, bool highlighted);
    void showDropHighlight(int index);
    void hideDropHighlight();
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
    void scheduleSaveChannels();
    void loadChannels();
    void updateGridColumnButtonState();
    QString channelsFilePath() const;
};
