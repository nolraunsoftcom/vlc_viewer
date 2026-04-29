#include "MainWindow.h"
#include "VlcWidget.h"
#include "StatusBar.h"
#include "Style.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDateTime>
#include <QHeaderView>
#include <QTabBar>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QMenu>
#include <QEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTextBlock>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QFileInfoList>
#include <QMessageBox>
#include <QProcess>
#include <QImageReader>
#include <QPainter>
#include <QStyle>
#include <QSizePolicy>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QFrame>
#include <QItemSelection>
#include <QItemSelectionModel>

static const int HEADER_HEIGHT = 32;
static const QString HEADER_STYLE = "background-color: #222; border-bottom: 1px solid #333;";
static const int MAX_LOG_LINES = 1000;
static const int GRID_SPACING = 1;
static const int VIEWER_INFO_BAR_HEIGHT = 36;
static const int LEFT_PANEL_WIDTH = 200;
static const int RIGHT_PANEL_WIDTH = 280;
static const int PANEL_TOGGLE_WIDTH = 18;
static const char *CHANNEL_DRAG_MIME = "application/x-ziilab-channel-row";
static const char *VIEWER_DRAG_MIME = "application/x-ziilab-viewer-ptr";
static const QString GRID_CELL_STYLE =
    "QFrame#gridCell { background-color: #111; border: none; }";
static const QString GRID_CELL_HIGHLIGHT_STYLE =
    "QFrame#gridCell { background-color: #4a9eff; border: none; }";

MainWindow::MainWindow(libvlc_instance_t *vlcInstance, QWidget *parent)
    : QMainWindow(parent)
    , m_vlcInstance(vlcInstance)
{
    setWindowTitle(QStringLiteral("영상관리시스템"));
    resize(1400, 800);

    auto *central = new QWidget(this);
    central->setStyleSheet("background-color: #1a1a1a;");
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    auto *panelRow = new QWidget(central);
    auto *mainLayout = new QHBoxLayout(panelRow);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_sidebar = new QWidget(panelRow);
    m_sidebar->setFixedWidth(LEFT_PANEL_WIDTH);
    m_sidebar->setStyleSheet("background-color: #1a1a1a;");
    setupSidebar(m_sidebar);
    mainLayout->addWidget(m_sidebar);

    m_leftPanelToggle = createPanelToggleButton(panelRow);
    connect(m_leftPanelToggle, &QPushButton::clicked, this, [this]() {
        setLeftPanelVisible(!m_leftPanelVisible);
    });
    mainLayout->addWidget(m_leftPanelToggle);

    auto *videoArea = new QWidget(panelRow);
    videoArea->setObjectName("videoArea");
    videoArea->setStyleSheet("#videoArea { background-color: black; border-left: 1px solid #333; border-right: 1px solid #333; }");
    setupVideoArea(videoArea);
    mainLayout->addWidget(videoArea, 1);

    m_rightPanelToggle = createPanelToggleButton(panelRow);
    connect(m_rightPanelToggle, &QPushButton::clicked, this, [this]() {
        setRightPanelVisible(!m_rightPanelVisible);
    });
    mainLayout->addWidget(m_rightPanelToggle);

    m_rightPanel = new QWidget(panelRow);
    m_rightPanel->setFixedWidth(RIGHT_PANEL_WIDTH);
    m_rightPanel->setStyleSheet("background-color: #1a1a1a;");
    setupRightPanel(m_rightPanel);
    mainLayout->addWidget(m_rightPanel);
    updatePanelToggleButtons();

    centralLayout->addWidget(panelRow, 1);

    m_statusBar = new StatusBar(central);
    centralLayout->addWidget(m_statusBar);

    setCentralWidget(central);

    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, [this]() {
        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        for (auto *viewer : m_viewers) {
            viewer->updateTime(timeStr);
        }
        // 전체화면 탭 내 복제본 VlcWidget 도 갱신 (녹화 경과시간 동기화)
        for (int i = 1; i < m_videoTabs->count(); ++i) {
            if (auto *v = qobject_cast<VlcWidget *>(m_videoTabs->widget(i))) {
                v->updateTime(timeStr);
            }
        }
        m_statusBar->updateStats(m_viewers);
    });
    m_clockTimer->start(1000);

    m_saveDebounceTimer = new QTimer(this);
    m_saveDebounceTimer->setSingleShot(true);
    m_saveDebounceTimer->setInterval(150);
    connect(m_saveDebounceTimer, &QTimer::timeout, this, &MainWindow::saveChannels);

    appendLog("System started", LogLevel::INFO);
    loadChannels();
    if (m_viewers.isEmpty()) {
        rebuildGrid();
    }
}

MainWindow::~MainWindow()
{
    if (m_saveDebounceTimer && m_saveDebounceTimer->isActive()) {
        m_saveDebounceTimer->stop();
        saveChannels();
    }

    for (auto *viewer : m_viewers) {
        viewer->stop();
    }
}

// ============================================================
// 헬퍼: 뷰어 생성 + 시그널 연결 (코드 중복 제거)
// ============================================================

VlcWidget *MainWindow::createViewer(const QString &name, const QString &url, bool autoReconnect)
{
    auto *viewer = new VlcWidget(m_vlcInstance, m_gridWidget);
    viewer->setAcceptDrops(true);
    viewer->installEventFilter(this);
    for (auto *child : viewer->findChildren<QWidget *>()) {
        child->setAcceptDrops(true);
        child->installEventFilter(this);
    }
    viewer->setLogCallback([this](const QString &msg, int level) {
        appendLog(msg, static_cast<LogLevel>(level));
    });
    viewer->setAutoReconnect(autoReconnect);
    connect(viewer, &VlcWidget::doubleClicked, this, &MainWindow::openFullscreenTab);
    connect(viewer, &VlcWidget::requestFullscreen, this, &MainWindow::openFullscreenTab);
    connect(viewer, &VlcWidget::requestEdit, this, [this](VlcWidget *v) {
        int idx = m_viewers.indexOf(v);
        if (idx >= 0) editChannel(idx);
    });
    connect(viewer, &VlcWidget::requestRemove, this, [this](VlcWidget *v) {
        int idx = m_viewers.indexOf(v);
        if (idx >= 0) {
            QString chName = v->name();

            // 해당 그리드 뷰어를 소스로 하는 orphan 전체화면 탭 정리
            for (int i = m_videoTabs->count() - 1; i >= 1; --i) {
                auto *tabWidget = m_videoTabs->widget(i);
                if (auto *fullViewer = qobject_cast<VlcWidget *>(tabWidget);
                    fullViewer && fullViewer->sourceViewer() == v) {
                    closeVideoTab(i);
                }
            }

            m_gridIndexes.remove(v);
            m_viewers.removeAt(idx);
            v->stop();
            v->deleteLater();
            m_channelTable->removeRow(idx);
            appendLog(QString("Channel removed: %1").arg(chName), LogLevel::WARN);
            saveChannels();
            rebuildGrid();
        }
    });
    connect(viewer, &VlcWidget::snapshotTaken, this, [this](const QString &path) {
        appendLog(QString("Snapshot: %1").arg(path), LogLevel::INFO);
        if (m_currentFileType == 0) refreshFilesList();
    });
    connect(viewer, &VlcWidget::recordingStarted, this,
        [this](VlcWidget *w, const QString &path) {
            appendLog(QString("[%1] Recording → %2").arg(w->name(), path), LogLevel::INFO);
        });
    connect(viewer, &VlcWidget::recordingStopped, this,
        [this](VlcWidget *w, const QString &path, qint64 bytes, int dur, bool byDisc) {
            double mb = bytes / (1024.0 * 1024.0);
            appendLog(QString("[%1] Recording %2: %3 (%4 MB, %5s)")
                .arg(w->name(), byDisc ? "auto-stopped" : "stopped", path)
                .arg(mb, 0, 'f', 1).arg(dur),
                byDisc ? LogLevel::WARN : LogLevel::INFO);
            if (m_currentFileType == 1) refreshFilesList();
        });
    connect(viewer, &VlcWidget::recordingFailed, this,
        [this](VlcWidget *w, const QString &path, const QString &reason) {
            appendLog(QString("[%1] Recording failed: %2 (%3)")
                .arg(w->name(), path, reason), LogLevel::ERROR);
        });
    connect(viewer, &VlcWidget::statusChanged, this, [this](VlcWidget *v, VlcWidget::Status s) {
        int idx = m_viewers.indexOf(v);
        if (idx < 0 || idx >= m_channelTable->rowCount()) return;
        auto *cell = m_channelTable->cellWidget(idx, 0);
        if (!cell) return;
        auto *statusLabel = cell->findChild<QLabel *>("statusLabel");
        if (!statusLabel) return;

        QString text = VlcWidget::statusText(s);
        QString color;
        switch (s) {
            case VlcWidget::Status::Connected:    color = "#4a4"; break;
            case VlcWidget::Status::Connecting:   color = "#888"; break;
            case VlcWidget::Status::Reconnecting: color = "#e8a838"; break;
            case VlcWidget::Status::Disconnected: color = "#e85050"; break;
            case VlcWidget::Status::Failed:       color = "#e85050"; break;
            default:                              color = "#888"; break;
        }
        statusLabel->setText(text);
        statusLabel->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent;").arg(color));
    });

    m_viewers.append(viewer);
    return viewer;
}

void MainWindow::addChannelToTable(const QString &name, const QString &url)
{
    int row = m_channelTable->rowCount();
    m_channelTable->insertRow(row);

    auto *cellWidget = new QWidget();
    cellWidget->setStyleSheet("background-color: transparent;");
    auto *cellLayout = new QVBoxLayout(cellWidget);
    cellLayout->setContentsMargins(8, 4, 8, 4);
    cellLayout->setSpacing(2);

    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name, cellWidget);
    nameLabel->setObjectName("nameLabel");
    nameLabel->setStyleSheet("color: white; font-size: 13px; font-weight: bold; background: transparent;");
    auto *statusLabel = new QLabel("대기", cellWidget);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setStyleSheet("color: #888; font-size: 10px; background: transparent;");
    topRow->addWidget(nameLabel);
    topRow->addStretch();
    topRow->addWidget(statusLabel);

    auto *urlLabel = new QLabel(url, cellWidget);
    urlLabel->setObjectName("urlLabel");
    urlLabel->setStyleSheet("color: #999; font-size: 11px; background: transparent;");
    urlLabel->setTextInteractionFlags(Qt::NoTextInteraction);

    cellLayout->addLayout(topRow);
    cellLayout->addWidget(urlLabel);

    m_channelTable->setCellWidget(row, 0, cellWidget);
    m_channelTable->setRowHeight(row, 46);
}

int MainWindow::effectiveGridCols() const
{
    int count = m_viewers.size();
    if (m_gridCols != 0) return m_gridCols;

    if (count == 0)      return 3;
    if (count <= 1)      return 1;
    else if (count <= 4) return 2;
    else if (count <= 9) return 3;
    else if (count <= 16) return 4;
    else                 return 5;
}

// ============================================================
// 좌측 사이드바
// ============================================================

void MainWindow::setupSidebar(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(parent);
    header->setFixedHeight(HEADER_HEIGHT);
    header->setStyleSheet(HEADER_STYLE);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    auto *title = new QLabel("ZiiLab 관제시스템", header);
    title->setStyleSheet("color: #aaa; font-size: 11px; font-weight: bold;");
    headerLayout->addWidget(title);
    layout->addWidget(header);

    auto *body = new QWidget(parent);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(8, 8, 8, 8);
    bodyLayout->setSpacing(4);

    m_channelTable = new QTableWidget(0, 1, body);
    m_channelTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelTable->viewport()->installEventFilter(this);
    m_channelTable->verticalHeader()->setVisible(false);
    m_channelTable->horizontalHeader()->setVisible(false);
    m_channelTable->horizontalHeader()->setStretchLastSection(true);
    m_channelTable->setShowGrid(true);
    m_channelTable->setStyleSheet(
        "QTableWidget { background-color: #1a1a1a; color: white; border: 1px solid #333; "
        "gridline-color: #333; }"
        "QTableWidget::item { background-color: #1a1a1a; }"
        "QTableWidget::item:selected { background-color: #2a3a5a; }");
    connect(m_channelTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_viewers.size()) {
            openFullscreenTab(m_viewers[row]);
        }
    });
    m_channelTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_channelTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        int row = m_channelTable->rowAt(pos.y());
        if (row < 0) return;

        m_channelTable->selectRow(row);

        QMenu menu(this);
        menu.setStyleSheet(Style::MENU);
        auto *fullscreenAction = menu.addAction("전체화면으로 열기");
        auto *editAction = menu.addAction("채널 수정");
        menu.addSeparator();
        auto *removeAction = menu.addAction("채널 삭제");

        auto *selected = menu.exec(m_channelTable->viewport()->mapToGlobal(pos));
        if (!selected) return;

        if (selected == fullscreenAction && row < m_viewers.size()) {
            openFullscreenTab(m_viewers[row]);
        } else if (selected == editAction) {
            editChannel(row);
        } else if (selected == removeAction) {
            removeSelectedChannel();
        }
    });
    bodyLayout->addWidget(m_channelTable, 1);

    auto *footerLayout = new QHBoxLayout();
    footerLayout->setContentsMargins(0, 4, 0, 0);
    footerLayout->setSpacing(2);

    auto *addBtn = new QPushButton("+", body);
    addBtn->setFixedHeight(26);
    addBtn->setStyleSheet(
        "QPushButton { color: white; background-color: #444; border: 1px solid #555; font-size: 13px; }"
        "QPushButton:hover { background-color: #555; }");
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addChannel);

    auto *removeBtn = new QPushButton("-", body);
    removeBtn->setFixedHeight(26);
    removeBtn->setStyleSheet(
        "QPushButton { color: white; background-color: #444; border: 1px solid #555; font-size: 13px; }"
        "QPushButton:hover { background-color: #555; }");
    connect(removeBtn, &QPushButton::clicked, this, &MainWindow::removeSelectedChannel);

    footerLayout->addWidget(addBtn);
    footerLayout->addWidget(removeBtn);
    bodyLayout->addLayout(footerLayout);

    layout->addWidget(body, 1);
}

// ============================================================
// 중앙 영상 영역
// ============================================================

void MainWindow::setupVideoArea(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_videoTabs = new QTabWidget(parent);
    m_videoTabs->setTabsClosable(false);
    m_videoTabs->tabBar()->setExpanding(false);
    m_videoTabs->tabBar()->setDocumentMode(true);
    m_videoTabs->tabBar()->setFixedHeight(HEADER_HEIGHT);
    m_videoTabs->setStyleSheet(
        "QTabWidget::pane { border: none; background-color: black; }"
        "QTabWidget::tab-bar { left: 0px; }"
        "QTabBar { background-color: #222; border-bottom: 1px solid #333; }"
        "QTabBar::tab { background-color: #222; color: #888; border: none; "
        "padding: 0px; font-size: 11px; min-width: 120px; min-height: 30px; }"
        "QTabBar::tab:selected { background-color: #111; color: white; "
        "border-bottom: 2px solid #4a9eff; }");

    m_gridPage = new QWidget();
    auto *gridPageLayout = new QVBoxLayout(m_gridPage);
    gridPageLayout->setContentsMargins(0, 0, 0, 0);
    gridPageLayout->setSpacing(0);

    m_gridScrollArea = new QScrollArea(m_gridPage);
    m_gridScrollArea->setWidgetResizable(true);
    m_gridScrollArea->setFrameShape(QFrame::NoFrame);
    m_gridScrollArea->setStyleSheet("QScrollArea { background-color: black; border: none; }");

    auto *scrollContent = new QWidget();
    scrollContent->setStyleSheet("background-color: black;");
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(0);

    m_gridWidget = new QWidget(scrollContent);
    m_gridWidget->setAcceptDrops(true);
    m_gridWidget->setStyleSheet("background-color: #333;");
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setSpacing(GRID_SPACING);
    m_grid->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(m_gridWidget);
    scrollLayout->addStretch(1);

    m_gridScrollArea->setWidget(scrollContent);
    gridPageLayout->addWidget(m_gridScrollArea, 1);

    m_videoTabs->addTab(m_gridPage, "전체");
    m_videoTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_videoTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

    m_gridWidget->installEventFilter(this);
    m_gridScrollArea->viewport()->installEventFilter(this);

    layout->addWidget(m_videoTabs, 1);
}

// ============================================================
// 좌우 패널 토글
// ============================================================

QPushButton *MainWindow::createPanelToggleButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedWidth(PANEL_TOGGLE_WIDTH);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { color: #aaa; background-color: #222; border: none; "
        "border-left: 1px solid #333; border-right: 1px solid #333; "
        "font-size: 12px; padding: 0; }"
        "QPushButton:hover { color: white; background-color: #333; }"
        "QPushButton:pressed { background-color: #111; }");
    return button;
}

void MainWindow::setLeftPanelVisible(bool visible)
{
    if (!m_sidebar || m_leftPanelVisible == visible) return;

    m_leftPanelVisible = visible;
    m_sidebar->setVisible(visible);
    updatePanelToggleButtons();
    QTimer::singleShot(0, this, [this]() {
        updateGridCellSizes();
    });
}

void MainWindow::setRightPanelVisible(bool visible)
{
    if (!m_rightPanel || m_rightPanelVisible == visible) return;

    m_rightPanelVisible = visible;
    m_rightPanel->setVisible(visible);
    updatePanelToggleButtons();
    QTimer::singleShot(0, this, [this]() {
        updateGridCellSizes();
    });
}

void MainWindow::updatePanelToggleButtons()
{
    if (m_leftPanelToggle) {
        m_leftPanelToggle->setText(m_leftPanelVisible ? "◀" : "▶");
        m_leftPanelToggle->setToolTip(m_leftPanelVisible ? "왼쪽 패널 숨기기" : "왼쪽 패널 보이기");
        m_leftPanelToggle->setAccessibleName(m_leftPanelToggle->toolTip());
    }
    if (m_rightPanelToggle) {
        m_rightPanelToggle->setText(m_rightPanelVisible ? "▶" : "◀");
        m_rightPanelToggle->setToolTip(m_rightPanelVisible ? "오른쪽 패널 숨기기" : "오른쪽 패널 보이기");
        m_rightPanelToggle->setAccessibleName(m_rightPanelToggle->toolTip());
    }
}

// ============================================================
// 우측 패널
// ============================================================

void MainWindow::setupRightPanel(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rightTabs = new QTabWidget(parent);
    m_rightTabs->tabBar()->setExpanding(false);
    m_rightTabs->tabBar()->setDocumentMode(true);
    m_rightTabs->tabBar()->setFixedHeight(HEADER_HEIGHT);
    m_rightTabs->setStyleSheet(
        "QTabWidget::pane { border: none; background-color: #1a1a1a; }"
        "QTabWidget::tab-bar { left: 0px; }"
        "QTabBar { background-color: #222; border-bottom: 1px solid #333; }"
        "QTabBar::tab { background-color: #222; color: #888; border: none; "
        "padding: 8px 16px; font-size: 11px; min-width: 50px; }"
        "QTabBar::tab:selected { background-color: #1a1a1a; color: white; "
        "border-bottom: 2px solid #4a9eff; }");

    // 화면 탭
    auto *settingsTab = new QWidget();
    auto *settingsLayout = new QVBoxLayout(settingsTab);
    settingsLayout->setContentsMargins(12, 12, 12, 12);
    settingsLayout->setSpacing(16);

    auto *colSection = new QLabel("그리드 컬럼", settingsTab);
    colSection->setStyleSheet("color: #888; font-size: 10px;");
    settingsLayout->addWidget(colSection);

    auto *colBtnLayout = new QHBoxLayout();
    colBtnLayout->setSpacing(4);

    m_colBtnGroup = new QButtonGroup(this);
    m_colBtnGroup->setExclusive(true);

    QStringList labels = {"Auto", "1", "2", "3", "4", "5"};
    QList<int> values = {0, 1, 2, 3, 4, 5};

    for (int i = 0; i < labels.size(); ++i) {
        auto *btn = new QPushButton(labels[i], settingsTab);
        btn->setCheckable(true);
        btn->setFixedHeight(28);
        btn->setMinimumWidth(38);
        btn->setStyleSheet(
            "QPushButton { color: #888; background-color: #222; border: 1px solid #444; "
            "border-radius: 3px; font-size: 11px; padding: 0 8px; }"
            "QPushButton:checked { color: white; background-color: #335; border-color: #4a9eff; }"
            "QPushButton:hover { background-color: #2a2a2a; }");
        m_colBtnGroup->addButton(btn, values[i]);
        colBtnLayout->addWidget(btn);
        if (values[i] == m_gridCols) btn->setChecked(true);
    }

    connect(m_colBtnGroup, &QButtonGroup::idClicked, this, &MainWindow::setGridColumns);
    settingsLayout->addLayout(colBtnLayout);
    updateGridColumnButtonState();
    settingsLayout->addStretch();
    m_rightTabs->addTab(settingsTab, "화면");

    // 녹화 탭
    auto *filesTab = new QWidget();
    setupFilesTab(filesTab);
    m_rightTabs->addTab(filesTab, "녹화");

    // 로그 탭
    auto *logTab = new QWidget();
    auto *logLayout = new QVBoxLayout(logTab);
    logLayout->setContentsMargins(0, 0, 0, 0);

    m_logView = new QTextEdit(logTab);
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "QTextEdit { background-color: #1a1a1a; color: #ccc; border: none; "
        "font-family: monospace; font-size: 11px; padding: 4px; }");
    logLayout->addWidget(m_logView);
    m_rightTabs->addTab(logTab, "로그");

    layout->addWidget(m_rightTabs);
}

// ============================================================
// 로그 (최대 라인 제한)
// ============================================================

void MainWindow::appendLog(const QString &message, LogLevel level)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString color, label;

    switch (level) {
        case LogLevel::DEBUG: color = "#666"; label = "DBG"; break;
        case LogLevel::INFO:  color = "#8cb4ff"; label = "INF"; break;
        case LogLevel::WARN:  color = "#e8a838"; label = "WRN"; break;
        case LogLevel::ERROR: color = "#e85050"; label = "ERR"; break;
    }

    m_logView->append(QString("<span style='color:#555'>[%1]</span> "
        "<span style='color:%2'>[%3]</span> "
        "<span style='color:#ccc'>%4</span>")
        .arg(timestamp, color, label, message));

    // 로그 라인 수 제한
    while (m_logView->document()->blockCount() > MAX_LOG_LINES) {
        QTextCursor cursor(m_logView->document()->begin());
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        cursor.deleteChar();
    }
}

// ============================================================
// 그리드 컬럼
// ============================================================

void MainWindow::setGridColumns(int cols)
{
    m_gridCols = cols;
    saveChannels();
    rebuildGrid();
    appendLog(QString("Grid columns: %1").arg(cols == 0 ? "Auto" : QString::number(cols)), LogLevel::DEBUG);
}

void MainWindow::updateGridColumnButtonState()
{
    if (!m_colBtnGroup) return;
    if (auto *button = m_colBtnGroup->button(m_gridCols)) {
        button->setChecked(true);
    }
}

// ============================================================
// 영상 탭 (더블클릭 전체화면) — 위젯 포인터로 추적
// ============================================================

void MainWindow::openFullscreenTab(VlcWidget *viewer)
{
    // 이미 열려있으면 해당 탭으로 전환
    for (int i = 1; i < m_videoTabs->count(); ++i) {
        auto *tabWidget = m_videoTabs->widget(i);
        if (auto *fullViewer = qobject_cast<VlcWidget *>(tabWidget);
            fullViewer && fullViewer->sourceViewer() == viewer) {
            m_videoTabs->setCurrentIndex(i);
            return;
        }
    }

    // 그리드 원본은 유지 — 탭엔 동일 URL의 독립 스트림을 띄움
    auto *fullViewer = new VlcWidget(m_vlcInstance, nullptr);
    fullViewer->setFullscreenMode(true);
    fullViewer->setSourceViewer(viewer);
    fullViewer->setLogCallback([this](const QString &msg, int level) {
        appendLog(msg, static_cast<LogLevel>(level));
    });
    connect(fullViewer, &VlcWidget::requestEdit, this, [this](VlcWidget *v) {
        int idx = m_viewers.indexOf(v);
        if (idx >= 0) editChannel(idx);
    });
    fullViewer->play(viewer->url(), viewer->name());

    // 원본 녹화 상태를 복제본 UI 에 즉시 반영 (이후 전이는 시그널이 릴레이)
    connect(viewer, &VlcWidget::recordingStateChanged,
        fullViewer, [fullViewer](VlcWidget *, VlcWidget::RecState) {
            fullViewer->updateRecordUi();
        });
    // 원본이 이미 녹화 중이면 첫 전이가 없으므로 prime
    fullViewer->updateRecordUi();

    int tabIndex = m_videoTabs->addTab(fullViewer, "");

    auto *tabLabel = new QLabel(viewer->name());
    tabLabel->setStyleSheet("color: #ccc; font-size: 11px; padding-left: 8px;");
    m_videoTabs->tabBar()->setTabButton(tabIndex, QTabBar::LeftSide, tabLabel);

    auto *closeBtn = new QPushButton("×");
    closeBtn->setFixedSize(18, 18);
    closeBtn->setStyleSheet(
        "QPushButton { color: #666; background: transparent; border: none; "
        "font-size: 12px; padding: 0; margin: 0; }"
        "QPushButton:hover { color: #ccc; background-color: #444; border-radius: 3px; }");
    connect(closeBtn, &QPushButton::clicked, this, [this, fullViewer]() {
        for (int i = 1; i < m_videoTabs->count(); ++i) {
            if (m_videoTabs->widget(i) == fullViewer) {
                closeVideoTab(i);
                return;
            }
        }
    });
    m_videoTabs->tabBar()->setTabButton(tabIndex, QTabBar::RightSide, closeBtn);
    m_videoTabs->setCurrentIndex(tabIndex);

    appendLog(QString("Fullscreen: %1").arg(viewer->name()), LogLevel::DEBUG);
}

void MainWindow::closeVideoTab(int index)
{
    if (index == 0) return;

    auto *widget = qobject_cast<VlcWidget *>(m_videoTabs->widget(index));
    m_videoTabs->removeTab(index);

    if (widget) {
        widget->stop();
        widget->deleteLater();
    }

    appendLog("Returned to grid", LogLevel::DEBUG);
}

// ============================================================
// 채널 저장/로드
// ============================================================

QString MainWindow::channelsFilePath() const
{
    QString dir = QDir::homePath() + "/.ziilab";
    QDir().mkpath(dir);
    return dir + "/channels.json";
}

void MainWindow::saveChannels()
{
    QJsonArray arr;
    for (auto *viewer : m_viewers) {
        QJsonObject obj;
        obj["name"] = viewer->name();
        obj["url"] = viewer->url();
        obj["autoReconnect"] = viewer->autoReconnect();
        obj["gridIndex"] = m_gridIndexes.contains(viewer)
            ? m_gridIndexes.value(viewer)
            : firstFreeGridIndex(viewer);
        arr.append(obj);
    }

    QJsonObject root;
    root["gridCols"] = m_gridCols;
    root["channels"] = arr;

    QFile file(channelsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
    }
}

void MainWindow::scheduleSaveChannels()
{
    if (!m_saveDebounceTimer) {
        saveChannels();
        return;
    }

    m_saveDebounceTimer->start();
}

void MainWindow::loadChannels()
{
    QFile file(channelsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else {
        QJsonObject root = doc.object();
        int savedGridCols = root["gridCols"].toInt(m_gridCols);
        if (savedGridCols >= 0 && savedGridCols <= 5) {
            m_gridCols = savedGridCols;
            updateGridColumnButtonState();
        }
        arr = root["channels"].toArray();
    }

    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        QString name = obj["name"].toString();
        QString url = obj["url"].toString();
        bool autoReconnect = obj.contains("autoReconnect") ? obj["autoReconnect"].toBool() : true;
        if (name.isEmpty() || url.isEmpty()) continue;

        auto *viewer = createViewer(name, url, autoReconnect);
        int gridIndex = obj.contains("gridIndex") ? obj["gridIndex"].toInt(-1) : -1;
        if (gridIndex < 0 || viewerAtGridIndex(gridIndex) != nullptr) {
            gridIndex = firstFreeGridIndex();
        }
        m_gridIndexes.insert(viewer, gridIndex);
        addChannelToTable(name, url);
        viewer->play(url, name);
        appendLog(QString("Channel loaded: %1").arg(name), LogLevel::DEBUG);
    }

    if (!m_viewers.isEmpty()) rebuildGrid();
}

// ============================================================
// 채널 추가/삭제
// ============================================================

void MainWindow::addChannel()
{
    auto info = ConnectionDialog::getConnectionInfo(this, m_viewers.size() + 1);
    if (!info) return;

    const QString name = info->channelName;
    const QString url  = info->rtspUrl;

    auto *viewer = createViewer(name, url, info->autoReconnect);
    m_gridIndexes.insert(viewer, firstFreeGridIndex());
    addChannelToTable(name, url);
    viewer->play(url, name);
    m_channelTable->selectRow(m_channelTable->rowCount() - 1);

    appendLog(QString("Channel added: %1 (%2)").arg(name, url), LogLevel::INFO);
    saveChannels();
    rebuildGrid();
}

void MainWindow::editChannel(int row)
{
    if (row < 0 || row >= m_viewers.size()) return;

    auto *viewer = m_viewers[row];
    const ConnectionInfo current{
        viewer->name(),
        viewer->url(),
        viewer->autoReconnect()
    };

    auto edited = ConnectionDialog::getConnectionInfo(this, current, "채널 수정");
    if (!edited) return;

    const QString name = edited->channelName.trimmed();
    const QString url = edited->rtspUrl.trimmed();
    if (name.isEmpty() || url.isEmpty()) return;

    const bool nameChanged = name != current.channelName;
    const bool urlChanged = url != current.rtspUrl;
    const bool autoReconnectChanged = edited->autoReconnect != current.autoReconnect;
    if (!nameChanged && !urlChanged && !autoReconnectChanged) return;

    viewer->setAutoReconnect(edited->autoReconnect);
    if (urlChanged) {
        viewer->play(url, name);
    } else if (nameChanged) {
        viewer->setChannelInfo(name, url);
    }

    for (int i = 1; i < m_videoTabs->count(); ++i) {
        auto *fullViewer = qobject_cast<VlcWidget *>(m_videoTabs->widget(i));
        if (!fullViewer || fullViewer->sourceViewer() != viewer) continue;

        fullViewer->setAutoReconnect(edited->autoReconnect);
        if (urlChanged) {
            fullViewer->play(url, name);
        } else if (nameChanged) {
            fullViewer->setChannelInfo(name, url);
        }

        if (auto *tabLabel = qobject_cast<QLabel *>(m_videoTabs->tabBar()->tabButton(i, QTabBar::LeftSide))) {
            tabLabel->setText(name);
        }
    }

    updateChannelTableRow(row, name, url);
    saveChannels();

    appendLog(QString("Channel updated: %1 (%2)").arg(name, url), LogLevel::INFO);
}

void MainWindow::updateChannelTableRow(int row, const QString &name, const QString &url)
{
    if (row < 0 || row >= m_channelTable->rowCount()) return;

    auto *cell = m_channelTable->cellWidget(row, 0);
    if (!cell) return;

    if (auto *nameLabel = cell->findChild<QLabel *>("nameLabel")) {
        nameLabel->setText(name);
    }
    if (auto *urlLabel = cell->findChild<QLabel *>("urlLabel")) {
        urlLabel->setText(url);
    }
}

void MainWindow::removeSelectedChannel()
{
    auto ranges = m_channelTable->selectedRanges();
    if (ranges.isEmpty()) return;

    QVector<int> rows;
    for (const auto &range : ranges) {
        for (int r = range.topRow(); r <= range.bottomRow(); ++r) {
            if (!rows.contains(r)) rows.append(r);
        }
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows) {
        if (row >= 0 && row < m_viewers.size()) {
            auto *viewer = m_viewers[row];
            QString chName = viewer->name();

            // orphan 전체화면 탭 정리
            for (int i = m_videoTabs->count() - 1; i >= 1; --i) {
                auto *tabWidget = m_videoTabs->widget(i);
                if (auto *fullViewer = qobject_cast<VlcWidget *>(tabWidget);
                    fullViewer && fullViewer->sourceViewer() == viewer) {
                    closeVideoTab(i);
                }
            }

            m_gridIndexes.remove(viewer);
            m_viewers.removeAt(row);
            viewer->stop();
            viewer->deleteLater();
            m_channelTable->removeRow(row);
            appendLog(QString("Channel removed: %1").arg(chName), LogLevel::WARN);
        }
    }

    saveChannels();
    rebuildGrid();
}

// ============================================================
// 그리드
// ============================================================

int MainWindow::firstFreeGridIndex(VlcWidget *except) const
{
    QSet<int> used;
    for (auto *viewer : m_viewers) {
        if (viewer == except) continue;
        int index = m_gridIndexes.value(viewer, -1);
        if (index >= 0) used.insert(index);
    }

    int index = 0;
    while (used.contains(index)) {
        ++index;
    }
    return index;
}

int MainWindow::maxAssignedGridIndex() const
{
    int maxIndex = -1;
    for (auto *viewer : m_viewers) {
        maxIndex = qMax(maxIndex, m_gridIndexes.value(viewer, -1));
    }
    return maxIndex;
}

VlcWidget *MainWindow::viewerAtGridIndex(int index, VlcWidget *except) const
{
    for (auto *viewer : m_viewers) {
        if (viewer == except) continue;
        if (m_gridIndexes.value(viewer, -1) == index) {
            return viewer;
        }
    }
    return nullptr;
}

void MainWindow::moveViewerToGridIndex(VlcWidget *viewer, int targetIndex)
{
    if (!viewer || targetIndex < 0 || !m_viewers.contains(viewer)) return;

    int sourceIndex = m_gridIndexes.contains(viewer)
        ? m_gridIndexes.value(viewer)
        : firstFreeGridIndex(viewer);
    if (sourceIndex == targetIndex) return;

    if (auto *other = viewerAtGridIndex(targetIndex, viewer)) {
        m_gridIndexes.insert(other, sourceIndex);
    }
    m_gridIndexes.insert(viewer, targetIndex);

    rebuildGrid();
    scheduleSaveChannels();
    appendLog(QString("Channel moved: %1 → grid index %2").arg(viewer->name()).arg(targetIndex + 1),
              LogLevel::DEBUG);
}

void MainWindow::rebuildGrid()
{
    hideDropHighlight();

    delete m_grid;
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setSpacing(GRID_SPACING);
    m_grid->setContentsMargins(0, 0, 0, 0);

    int cols = effectiveGridCols();

    for (int c = 0; c < cols; ++c) m_grid->setColumnStretch(c, 1);

    for (auto *viewer : m_viewers) {
        int index = m_gridIndexes.value(viewer, -1);
        if (index < 0 || (viewerAtGridIndex(index, viewer) != nullptr)) {
            index = firstFreeGridIndex(viewer);
            m_gridIndexes.insert(viewer, index);
        }
        viewer->setProperty("gridIndex", index);
    }

    updateGridCellSizes();
}

bool MainWindow::startChannelDragIfNeeded(QMouseEvent *event)
{
    if (!event || m_channelDragRow < 0) return false;
    if (!(event->buttons() & Qt::LeftButton)) return false;
    if ((event->position().toPoint() - m_channelDragStartPos).manhattanLength()
        < QApplication::startDragDistance()) {
        return false;
    }
    if (m_channelDragRow >= m_viewers.size()) return false;

    m_channelDragStarted = true;
    auto *drag = new QDrag(m_channelTable);
    auto *mimeData = new QMimeData();
    mimeData->setData(CHANNEL_DRAG_MIME, QByteArray::number(m_channelDragRow));
    mimeData->setText(m_viewers[m_channelDragRow]->name());
    drag->setMimeData(mimeData);

    drag->exec(Qt::MoveAction);
    hideDropHighlight();
    m_channelTable->clearSelection();
    m_channelTable->setCurrentIndex(QModelIndex());
    if (auto *selection = m_channelTable->selectionModel()) {
        selection->clear();
    }
    m_lastChannelClickedRow = -1;
    m_channelDragRow = -1;
    return true;
}

VlcWidget *MainWindow::viewerForDragObject(QObject *obj) const
{
    for (QObject *current = obj; current; current = current->parent()) {
        if (auto *viewer = qobject_cast<VlcWidget *>(current)) {
            return m_viewers.contains(viewer) ? viewer : nullptr;
        }
        if (current == m_gridWidget) break;
    }
    return nullptr;
}

bool MainWindow::startViewerDragIfNeeded(QMouseEvent *event)
{
    if (!event || !m_viewerDragSource) return false;
    if (!(event->buttons() & Qt::LeftButton)) return false;
    if ((event->position().toPoint() - m_viewerDragStartPos).manhattanLength()
        < QApplication::startDragDistance()) {
        return false;
    }

    m_viewerDragStarted = true;

    auto *drag = new QDrag(m_viewerDragSource);
    auto *mimeData = new QMimeData();
    quintptr ptr = reinterpret_cast<quintptr>(m_viewerDragSource);
    mimeData->setData(VIEWER_DRAG_MIME, QByteArray::number(ptr));
    mimeData->setText(m_viewerDragSource->name());
    drag->setMimeData(mimeData);

    drag->exec(Qt::MoveAction);
    hideDropHighlight();
    m_viewerDragSource = nullptr;
    m_viewerDragStarted = false;
    return true;
}

void MainWindow::selectChannelRowFromClick(int row, Qt::KeyboardModifiers modifiers)
{
    if (!m_channelTable || row < 0 || row >= m_channelTable->rowCount()) {
        if (m_channelTable) {
            m_channelTable->clearSelection();
            m_channelTable->setCurrentIndex(QModelIndex());
        }
        m_lastChannelClickedRow = -1;
        return;
    }

    auto *selection = m_channelTable->selectionModel();
    auto *model = m_channelTable->model();
    if (!selection || !model) return;

    QModelIndex index = model->index(row, 0);
    const bool additive = modifiers.testFlag(Qt::ControlModifier)
        || modifiers.testFlag(Qt::MetaModifier);
    const bool range = modifiers.testFlag(Qt::ShiftModifier)
        && m_lastChannelClickedRow >= 0
        && m_lastChannelClickedRow < m_channelTable->rowCount();

    if (range) {
        int from = qMin(m_lastChannelClickedRow, row);
        int to = qMax(m_lastChannelClickedRow, row);
        QItemSelection rangeSelection(model->index(from, 0), model->index(to, 0));
        selection->select(rangeSelection,
                          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else if (additive) {
        QItemSelection rowSelection(index, index);
        bool selected = selection->isRowSelected(row, QModelIndex());
        selection->select(rowSelection,
                          (selected ? QItemSelectionModel::Deselect : QItemSelectionModel::Select)
                              | QItemSelectionModel::Rows);
        m_lastChannelClickedRow = row;
    } else {
        QItemSelection rowSelection(index, index);
        selection->select(rowSelection,
                          QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        m_lastChannelClickedRow = row;
    }

    selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
}

int MainWindow::gridIndexForDropTarget(QObject *obj, const QPoint &pos) const
{
    if (!obj) return -1;

    for (QObject *current = obj; current; current = current->parent()) {
        QVariant directIndex = current->property("gridIndex");
        if (directIndex.isValid()) {
            bool ok = false;
            int index = directIndex.toInt(&ok);
            if (ok && index >= 0) return index;
        }
        if (current == m_gridWidget) break;
    }

    auto *widget = qobject_cast<QWidget *>(obj);
    if (!widget || !m_gridWidget) return -1;

    QPoint gridPos = (widget == m_gridWidget) ? pos : widget->mapTo(m_gridWidget, pos);
    if (gridPos.x() < 0 || gridPos.y() < 0) return -1;

    int cols = effectiveGridCols();
    int gridWidth = m_gridScrollArea ? m_gridScrollArea->viewport()->width() : m_gridWidget->width();
    int cellWidth = qMax(1, (gridWidth - (cols - 1) * GRID_SPACING) / cols);
    int cellHeight = cellWidth * 3 / 4 + VIEWER_INFO_BAR_HEIGHT;
    int strideX = cellWidth + GRID_SPACING;
    int strideY = cellHeight + GRID_SPACING;
    int col = gridPos.x() / strideX;
    int row = gridPos.y() / strideY;

    if (col < 0 || col >= cols) return -1;
    return row * cols + col;
}

QFrame *MainWindow::createGridCell()
{
    auto *cell = new QFrame(m_gridWidget);
    cell->setObjectName("gridCell");
    cell->setAcceptDrops(true);
    cell->installEventFilter(this);
    cell->setProperty("dropHighlighted", false);
    cell->setStyleSheet(GRID_CELL_STYLE);

    auto *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    return cell;
}

void MainWindow::clearGridCell(QFrame *cell)
{
    if (!cell || !cell->layout()) return;

    auto *layout = cell->layout();
    while (auto *item = layout->takeAt(0)) {
        if (auto *widget = item->widget()) {
            if (auto *viewer = qobject_cast<VlcWidget *>(widget)) {
                viewer->setParent(m_gridWidget);
                viewer->hide();
            } else {
                widget->deleteLater();
            }
        }
        delete item;
    }
}

void MainWindow::ensureGridCellCount(int count)
{
    while (m_gridCells.size() < count) {
        m_gridCells.append(createGridCell());
    }

    while (m_gridCells.size() > count) {
        auto *cell = m_gridCells.takeLast();
        clearGridCell(cell);
        m_grid->removeWidget(cell);
        delete cell;
    }
}

void MainWindow::setGridCellHighlighted(int index, bool highlighted)
{
    if (index < 0 || index >= m_gridCells.size()) return;
    auto *cell = m_gridCells[index];
    if (cell->property("dropHighlighted").toBool() == highlighted) return;

    cell->setProperty("dropHighlighted", highlighted);
    cell->setStyleSheet(highlighted ? GRID_CELL_HIGHLIGHT_STYLE : GRID_CELL_STYLE);
}

void MainWindow::showDropHighlight(int index)
{
    if (index < 0 || index == m_dropHighlightIndex) return;

    setGridCellHighlighted(m_dropHighlightIndex, false);
    m_dropHighlightIndex = index;
    setGridCellHighlighted(m_dropHighlightIndex, true);
}

void MainWindow::hideDropHighlight()
{
    if (m_dropHighlightIndex < 0) return;

    setGridCellHighlighted(m_dropHighlightIndex, false);
    m_dropHighlightIndex = -1;
}

bool MainWindow::handleGridDragEvent(QObject *obj, QEvent *event)
{
    if (!event) return false;

    auto hasChannelMime = [](const QMimeData *mimeData) {
        return mimeData
            && (mimeData->hasFormat(CHANNEL_DRAG_MIME)
                || mimeData->hasFormat(VIEWER_DRAG_MIME));
    };

    if (event->type() == QEvent::DragEnter) {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        if (!hasChannelMime(dragEvent->mimeData())) return false;

        int targetIndex = gridIndexForDropTarget(obj, dragEvent->position().toPoint());
        if (targetIndex < 0) {
            hideDropHighlight();
            return false;
        }

        showDropHighlight(targetIndex);
        dragEvent->acceptProposedAction();
        return true;
    }

    if (event->type() == QEvent::DragMove) {
        auto *dragEvent = static_cast<QDragMoveEvent *>(event);
        if (!hasChannelMime(dragEvent->mimeData())) return false;

        int targetIndex = gridIndexForDropTarget(obj, dragEvent->position().toPoint());
        if (targetIndex < 0) {
            hideDropHighlight();
            return false;
        }

        showDropHighlight(targetIndex);
        dragEvent->acceptProposedAction();
        return true;
    }

    if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (!hasChannelMime(dropEvent->mimeData())) return false;

        int targetIndex = gridIndexForDropTarget(obj, dropEvent->position().toPoint());
        bool ok = false;
        VlcWidget *viewer = nullptr;
        if (dropEvent->mimeData()->hasFormat(CHANNEL_DRAG_MIME)) {
            int row = dropEvent->mimeData()->data(CHANNEL_DRAG_MIME).toInt(&ok);
            if (ok && row >= 0 && row < m_viewers.size()) {
                viewer = m_viewers[row];
            }
        } else if (dropEvent->mimeData()->hasFormat(VIEWER_DRAG_MIME)) {
            quintptr ptr = dropEvent->mimeData()->data(VIEWER_DRAG_MIME).toULongLong(&ok);
            auto *candidate = reinterpret_cast<VlcWidget *>(ptr);
            if (ok && m_viewers.contains(candidate)) {
                viewer = candidate;
            }
        }

        if (targetIndex < 0 || !viewer) {
            hideDropHighlight();
            return false;
        }

        hideDropHighlight();
        moveViewerToGridIndex(viewer, targetIndex);
        dropEvent->acceptProposedAction();
        return true;
    }

    return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (m_channelTable && obj == m_channelTable->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_channelDragStartPos = mouseEvent->position().toPoint();
                m_channelDragRow = m_channelTable->rowAt(m_channelDragStartPos.y());
                m_channelDragStarted = false;
                if (m_channelDragRow >= 0) {
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_channelDragRow >= 0) {
                startChannelDragIfNeeded(static_cast<QMouseEvent *>(event));
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                int pressRow = m_channelDragRow;
                bool wasDrag = m_channelDragStarted;
                int releaseRow = m_channelTable->rowAt(mouseEvent->position().toPoint().y());
                m_channelDragRow = -1;
                m_channelDragStarted = false;

                if (!wasDrag && pressRow >= 0 && pressRow == releaseRow) {
                    selectChannelRowFromClick(pressRow, mouseEvent->modifiers());
                }
                return true;
            }
            m_channelDragRow = -1;
            m_channelDragStarted = false;
        }
    }

    const bool isViewerMouseEvent =
        event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseMove
        || event->type() == QEvent::MouseButtonRelease;
    if (isViewerMouseEvent) {
        auto *viewer = viewerForDragObject(obj);
        if (viewer && event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_viewerDragSource = viewer;
                m_viewerDragStartPos = mouseEvent->position().toPoint();
                m_viewerDragStarted = false;
            }
        } else if (viewer && event->type() == QEvent::MouseMove) {
            if (m_viewerDragSource == viewer) {
                if (startViewerDragIfNeeded(static_cast<QMouseEvent *>(event))) {
                    return true;
                }
            }
        } else if (viewer && event->type() == QEvent::MouseButtonRelease) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_viewerDragSource == viewer) {
                m_viewerDragSource = nullptr;
                m_viewerDragStarted = false;
            }
        }
    }

    if (handleGridDragEvent(obj, event)) {
        return true;
    }

    const bool isGridResize = obj == m_gridWidget && event->type() == QEvent::Resize;
    const bool isViewportResize = m_gridScrollArea
        && obj == m_gridScrollArea->viewport()
        && event->type() == QEvent::Resize;
    if (isGridResize || isViewportResize) {
        updateGridCellSizes();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::updateGridCellSizes()
{
    if (!m_grid) return;

    int cols = effectiveGridCols();
    int gridWidth = m_gridScrollArea ? m_gridScrollArea->viewport()->width() : m_gridWidget->width();
    if (gridWidth <= 0) {
        gridWidth = m_gridWidget->parentWidget() ? m_gridWidget->parentWidget()->width() : m_gridWidget->width();
    }
    int viewportHeight = m_gridScrollArea ? m_gridScrollArea->viewport()->height() : m_gridWidget->height();
    if (viewportHeight <= 0) {
        viewportHeight = m_gridWidget->parentWidget() ? m_gridWidget->parentWidget()->height() : m_gridWidget->height();
    }

    int cellWidth = qMax(1, (gridWidth - (cols - 1) * GRID_SPACING) / cols);
    int cellHeight = cellWidth * 3 / 4 + VIEWER_INFO_BAR_HEIGHT;
    int requiredRows = qMax(1, (maxAssignedGridIndex() + cols) / cols);
    int rowsForViewport = qMax(1, (viewportHeight + GRID_SPACING + cellHeight) / (cellHeight + GRID_SPACING));
    int rows = qMax(requiredRows, rowsForViewport);
    int totalCells = rows * cols;

    QHash<int, VlcWidget *> occupied;
    for (auto *viewer : m_viewers) {
        int index = m_gridIndexes.value(viewer, -1);
        if (index >= 0 && index < totalCells) {
            occupied.insert(index, viewer);
        }
    }

    ensureGridCellCount(totalCells);

    for (auto *viewer : m_viewers) {
        viewer->setFixedSize(qMax(1, cellWidth - 4), qMax(1, cellHeight - 4));
    }

    for (int i = 0; i < m_gridCells.size(); ++i) {
        auto *cell = m_gridCells[i];
        cell->setProperty("gridIndex", i);
        cell->setFixedSize(cellWidth, cellHeight);
        m_grid->addWidget(cell, i / cols, i % cols);
        setGridCellHighlighted(i, i == m_dropHighlightIndex);

        auto *layout = qobject_cast<QVBoxLayout *>(cell->layout());
        if (!layout) continue;

        VlcWidget *viewer = occupied.value(i, nullptr);
        QWidget *current = layout->count() > 0 ? layout->itemAt(0)->widget() : nullptr;

        if (viewer) {
            if (current != viewer) {
                clearGridCell(cell);
                if (auto *oldParent = viewer->parentWidget(); oldParent && oldParent->layout()) {
                    oldParent->layout()->removeWidget(viewer);
                }
                viewer->setParent(cell);
                layout->addWidget(viewer);
            }
            viewer->show();
        } else if (!qobject_cast<QLabel *>(current)) {
            clearGridCell(cell);
            auto *label = new QLabel("No Stream", cell);
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet("color: #444; font-size: 13px; background-color: #111;");
            label->setAcceptDrops(true);
            label->installEventFilter(this);
            layout->addWidget(label);
        }
        cell->show();
    }

    int gridHeight = rows * cellHeight + (rows - 1) * GRID_SPACING;
    m_gridWidget->setFixedHeight(gridHeight);
}

// ============================================================
// 녹화 탭 (스냅샷/녹화 목록)
// ============================================================

QString MainWindow::snapshotsDir()
{
    return QDir::homePath() + "/.ziilab/snapshots";
}

QString MainWindow::recordingsDir()
{
    return QDir::homePath() + "/.ziilab/recordings";
}

QString MainWindow::currentFilesDir() const
{
    return m_currentFileType == 1 ? recordingsDir() : snapshotsDir();
}

void MainWindow::setupFilesTab(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 타입 토글 (스냅샷/녹화)
    auto *typeLayout = new QHBoxLayout();
    typeLayout->setSpacing(4);

    m_fileTypeGroup = new QButtonGroup(this);
    m_fileTypeGroup->setExclusive(true);

    QStringList typeLabels = {"스냅샷", "녹화"};
    for (int i = 0; i < typeLabels.size(); ++i) {
        auto *btn = new QPushButton(typeLabels[i], parent);
        btn->setCheckable(true);
        btn->setFixedHeight(28);
        btn->setStyleSheet(
            "QPushButton { color: #888; background-color: #222; border: 1px solid #444; "
            "border-radius: 3px; font-size: 11px; padding: 0 12px; }"
            "QPushButton:checked { color: white; background-color: #335; border-color: #4a9eff; }"
            "QPushButton:hover { background-color: #2a2a2a; }");
        m_fileTypeGroup->addButton(btn, i);
        typeLayout->addWidget(btn);
        if (i == 0) btn->setChecked(true);
    }
    typeLayout->addStretch();

    const QString iconBtnStyle =
        "QPushButton { color: #aaa; background-color: #222; border: 1px solid #444; "
        "border-radius: 3px; font-size: 13px; }"
        "QPushButton:hover { background-color: #2a2a2a; }";

    auto *refreshBtn = new QPushButton("↻", parent);
    refreshBtn->setFixedSize(28, 28);
    refreshBtn->setToolTip("새로고침");
    refreshBtn->setStyleSheet(iconBtnStyle);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshFilesList);
    typeLayout->addWidget(refreshBtn);

    auto *openDirBtn = new QPushButton("📁", parent);
    openDirBtn->setFixedSize(28, 28);
    openDirBtn->setToolTip("폴더 열기");
    openDirBtn->setStyleSheet(iconBtnStyle);
    connect(openDirBtn, &QPushButton::clicked, this, [this]() {
        QString dir = currentFilesDir();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    typeLayout->addWidget(openDirBtn);

    layout->addLayout(typeLayout);

    connect(m_fileTypeGroup, &QButtonGroup::idClicked, this, &MainWindow::setFileType);

    // 파일 목록
    m_filesList = new QListWidget(parent);
    m_filesList->setIconSize(QSize(64, 48));
    m_filesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_filesList->setWordWrap(true);
    m_filesList->setTextElideMode(Qt::ElideNone);
    m_filesList->setResizeMode(QListView::Adjust);
    m_filesList->setStyleSheet(
        "QListWidget { background-color: #1a1a1a; color: #ccc; border: 1px solid #333; "
        "font-size: 11px; outline: none; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #222; }"
        "QListWidget::item:selected { background-color: #2a3a5a; color: white; }"
        "QListWidget::item:hover { background-color: #222; }");
    m_filesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_filesList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::openFilesListItem);
    connect(m_filesList, &QListWidget::customContextMenuRequested,
            this, &MainWindow::showFilesContextMenu);

    layout->addWidget(m_filesList, 1);

    refreshFilesList();
}

void MainWindow::setFileType(int type)
{
    m_currentFileType = type;
    refreshFilesList();
}

static QIcon makeImageThumb(const QString &path, const QSize &target)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize orig = reader.size();
    if (orig.isValid()) {
        QSize scaled = orig.scaled(target, Qt::KeepAspectRatio);
        reader.setScaledSize(scaled);
    }
    QImage img = reader.read();
    if (img.isNull()) return {};
    return QIcon(QPixmap::fromImage(img));
}

static QIcon makeVideoThumb(const QSize &target)
{
    // 필름 스트립 스타일의 플레이스홀더
    QPixmap pm(target);
    pm.fill(QColor("#2a2a2a"));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // 필름 스트립 양옆 구멍
    p.setBrush(QColor("#111"));
    p.setPen(Qt::NoPen);
    int h = target.height();
    int w = target.width();
    int holeW = 6, holeH = 4, gap = 2;
    for (int y = 4; y + holeH <= h - 4; y += holeH + gap) {
        p.drawRect(3, y, holeW, holeH);
        p.drawRect(w - 3 - holeW, y, holeW, holeH);
    }

    // 중앙 플레이 삼각형
    p.setBrush(QColor("#8cb4ff"));
    int cx = w / 2, cy = h / 2, r = h / 4;
    QPolygon tri;
    tri << QPoint(cx - r + 2, cy - r)
        << QPoint(cx - r + 2, cy + r)
        << QPoint(cx + r + 2, cy);
    p.drawPolygon(tri);
    return QIcon(pm);
}

void MainWindow::refreshFilesList()
{
    if (!m_filesList) return;
    m_filesList->clear();

    QDir dir(currentFilesDir());
    if (!dir.exists()) return;

    const bool isVideo = (m_currentFileType == 1);
    QStringList filters = isVideo
        ? QStringList{"*.mp4", "*.mkv", "*.mov"}
        : QStringList{"*.png", "*.jpg", "*.jpeg"};

    const QSize iconSize = m_filesList->iconSize();
    QIcon videoThumb; // 비디오용 공용 플레이스홀더 (한 번만 생성)

    auto entries = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    for (const auto &info : entries) {
        QString when = info.lastModified().toString("yyyy-MM-dd HH:mm");
        QString size;
        qint64 bytes = info.size();
        if (bytes < 1024)              size = QString("%1 B").arg(bytes);
        else if (bytes < 1024 * 1024)  size = QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        else if (bytes < 1024LL * 1024 * 1024)
            size = QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        else
            size = QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);

        auto *item = new QListWidgetItem(
            QString("%1\n%2\n%3").arg(info.fileName(), when, size));
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());

        if (isVideo) {
            if (videoThumb.isNull()) videoThumb = makeVideoThumb(iconSize);
            item->setIcon(videoThumb);
        } else {
            QIcon thumb = makeImageThumb(info.absoluteFilePath(), iconSize);
            if (!thumb.isNull()) item->setIcon(thumb);
        }

        m_filesList->addItem(item);
    }
}

void MainWindow::openFilesListItem(QListWidgetItem *item)
{
    if (!item) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->data(Qt::UserRole).toString()));
}

void MainWindow::showFilesContextMenu(const QPoint &pos)
{
    auto *item = m_filesList->itemAt(pos);
    if (!item) return;

    const QString path = item->data(Qt::UserRole).toString();

    QMenu menu(this);
    menu.setStyleSheet(Style::MENU);
    auto *openAction = menu.addAction("열기");
    auto *revealAction = menu.addAction(
#if defined(__APPLE__)
        "Finder에서 보기"
#elif defined(_WIN32)
        "탐색기에서 보기"
#else
        "파일 관리자에서 보기"
#endif
    );
    menu.addSeparator();
    auto *deleteAction = menu.addAction("삭제");

    auto *selected = menu.exec(m_filesList->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == openAction) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else if (selected == revealAction) {
#if defined(__APPLE__)
        QProcess::startDetached("open", {"-R", path});
#elif defined(_WIN32)
        QProcess::startDetached("explorer", {"/select,", QDir::toNativeSeparators(path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    } else if (selected == deleteAction) {
        auto ret = QMessageBox::question(this, "파일 삭제",
            QString("'%1' 파일을 삭제하시겠습니까?").arg(QFileInfo(path).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret == QMessageBox::Yes) {
            if (QFile::remove(path)) {
                appendLog(QString("파일 삭제: %1").arg(path), LogLevel::INFO);
                refreshFilesList();
            } else {
                appendLog(QString("파일 삭제 실패: %1").arg(path), LogLevel::ERROR);
            }
        }
    }
}
