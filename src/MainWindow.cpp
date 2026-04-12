#include "MainWindow.h"
#include "VlcWidget.h"
#include "StatusBar.h"
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

static const int HEADER_HEIGHT = 32;
static const QString HEADER_STYLE = "background-color: #222; border-bottom: 1px solid #333;";
static const int MAX_LOG_LINES = 1000;

MainWindow::MainWindow(libvlc_instance_t *vlcInstance, QWidget *parent)
    : QMainWindow(parent)
    , m_vlcInstance(vlcInstance)
{
    setWindowTitle("ZiiLab Viewer");
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

    auto *sidebar = new QWidget(panelRow);
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet("background-color: #1a1a1a;");
    setupSidebar(sidebar);
    mainLayout->addWidget(sidebar);

    auto *videoArea = new QWidget(panelRow);
    videoArea->setObjectName("videoArea");
    videoArea->setStyleSheet("#videoArea { background-color: black; border-left: 1px solid #333; border-right: 1px solid #333; }");
    setupVideoArea(videoArea);
    mainLayout->addWidget(videoArea, 1);

    auto *rightPanel = new QWidget(panelRow);
    rightPanel->setFixedWidth(280);
    rightPanel->setStyleSheet("background-color: #1a1a1a;");
    setupRightPanel(rightPanel);
    mainLayout->addWidget(rightPanel);

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
        m_statusBar->updateStats(m_viewers);
    });
    m_clockTimer->start(1000);

    appendLog("System started", LogLevel::INFO);
    loadChannels();
}

MainWindow::~MainWindow()
{
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
    viewer->setLogCallback([this](const QString &msg, int level) {
        appendLog(msg, static_cast<LogLevel>(level));
    });
    viewer->setAutoReconnect(autoReconnect);
    connect(viewer, &VlcWidget::doubleClicked, this, &MainWindow::openFullscreenTab);
    connect(viewer, &VlcWidget::requestFullscreen, this, &MainWindow::openFullscreenTab);
    connect(viewer, &VlcWidget::requestRemove, this, [this](VlcWidget *v) {
        int idx = m_viewers.indexOf(v);
        if (idx >= 0) {
            QString chName = v->name();

            // orphan fullscreen 탭 닫기
            for (int i = m_videoTabs->count() - 1; i >= 1; --i) {
                auto *tabWidget = m_videoTabs->widget(i);
                if (tabWidget && tabWidget->property("sourceViewer").value<quintptr>() == reinterpret_cast<quintptr>(v)) {
                    closeVideoTab(i);
                }
            }

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
    nameLabel->setStyleSheet("color: white; font-size: 13px; font-weight: bold; background: transparent;");
    auto *statusLabel = new QLabel("대기", cellWidget);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setStyleSheet("color: #888; font-size: 10px; background: transparent;");
    topRow->addWidget(nameLabel);
    topRow->addStretch();
    topRow->addWidget(statusLabel);

    auto *urlLabel = new QLabel(url, cellWidget);
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
        menu.setStyleSheet(
            "QMenu { background-color: #2a2a2a; color: #ccc; border: 1px solid #444; font-size: 12px; }"
            "QMenu::item { padding: 6px 20px; }"
            "QMenu::item:selected { background-color: #335; }");
        auto *fullscreenAction = menu.addAction("전체화면으로 열기");
        menu.addSeparator();
        auto *removeAction = menu.addAction("채널 삭제");

        auto *selected = menu.exec(m_channelTable->viewport()->mapToGlobal(pos));
        if (!selected) return;

        if (selected == fullscreenAction && row < m_viewers.size()) {
            openFullscreenTab(m_viewers[row]);
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

    auto *scrollArea = new QScrollArea(m_gridPage);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background-color: black; border: none; }");

    auto *scrollContent = new QWidget();
    scrollContent->setStyleSheet("background-color: black;");
    auto *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(0);

    m_gridWidget = new QWidget(scrollContent);
    m_gridWidget->setStyleSheet("background-color: #333;");
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setSpacing(1);
    m_grid->setContentsMargins(0, 0, 0, 0);
    scrollLayout->addWidget(m_gridWidget);
    scrollLayout->addStretch(1);

    scrollArea->setWidget(scrollContent);
    gridPageLayout->addWidget(scrollArea, 1);

    m_videoTabs->addTab(m_gridPage, "전체");
    m_videoTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_videoTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

    scrollArea->installEventFilter(this);
    m_gridWidget->installEventFilter(this);

    layout->addWidget(m_videoTabs, 1);
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

    // 설정 탭
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
        if (i == 0) btn->setChecked(true);
    }

    connect(m_colBtnGroup, &QButtonGroup::idClicked, this, &MainWindow::setGridColumns);
    settingsLayout->addLayout(colBtnLayout);
    settingsLayout->addStretch();
    m_rightTabs->addTab(settingsTab, "설정");

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
    rebuildGrid();
    appendLog(QString("Grid columns: %1").arg(cols == 0 ? "Auto" : QString::number(cols)), LogLevel::DEBUG);
}

// ============================================================
// 영상 탭 (더블클릭 전체화면) — 위젯 포인터로 추적
// ============================================================

void MainWindow::openFullscreenTab(VlcWidget *viewer)
{
    // 이미 열려있는지 소스 뷰어 포인터로 확인
    for (int i = 1; i < m_videoTabs->count(); ++i) {
        auto *tabWidget = m_videoTabs->widget(i);
        if (tabWidget && tabWidget->property("sourceViewer").value<quintptr>() == reinterpret_cast<quintptr>(viewer)) {
            m_videoTabs->setCurrentIndex(i);
            return;
        }
    }

    auto *fullViewer = new VlcWidget(m_vlcInstance, nullptr);
    fullViewer->setFullscreenMode(true);
    fullViewer->setProperty("sourceViewer", QVariant::fromValue(reinterpret_cast<quintptr>(viewer)));
    fullViewer->setLogCallback([this](const QString &msg, int level) {
        appendLog(msg, static_cast<LogLevel>(level));
    });
    fullViewer->play(viewer->url(), viewer->name());

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
        arr.append(obj);
    }

    QFile file(channelsFilePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
    }
}

void MainWindow::loadChannels()
{
    QFile file(channelsFilePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
    for (const auto &val : arr) {
        QJsonObject obj = val.toObject();
        QString name = obj["name"].toString();
        QString url = obj["url"].toString();
        bool autoReconnect = obj.contains("autoReconnect") ? obj["autoReconnect"].toBool() : true;
        if (name.isEmpty() || url.isEmpty()) continue;

        auto *viewer = createViewer(name, url, autoReconnect);
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
    addChannelToTable(name, url);
    viewer->play(url, name);
    m_channelTable->selectRow(m_channelTable->rowCount() - 1);

    appendLog(QString("Channel added: %1 (%2)").arg(name, url), LogLevel::INFO);
    saveChannels();
    rebuildGrid();
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

            // orphan fullscreen 탭 닫기
            for (int i = m_videoTabs->count() - 1; i >= 1; --i) {
                auto *tabWidget = m_videoTabs->widget(i);
                if (tabWidget && tabWidget->property("sourceViewer").value<quintptr>() == reinterpret_cast<quintptr>(viewer)) {
                    closeVideoTab(i);
                }
            }

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

void MainWindow::rebuildGrid()
{
    qDeleteAll(m_emptyLabels);
    m_emptyLabels.clear();

    for (auto *viewer : m_viewers) {
        m_grid->removeWidget(viewer);
        viewer->hide();
    }

    delete m_grid;
    m_grid = new QGridLayout(m_gridWidget);
    m_grid->setSpacing(1);
    m_grid->setContentsMargins(0, 0, 0, 0);

    int count = m_viewers.size();
    if (count == 0) return;

    int cols = effectiveGridCols();
    int rows = (count + cols - 1) / cols;
    int totalCells = rows * cols;

    for (int c = 0; c < cols; ++c) m_grid->setColumnStretch(c, 1);

    for (int i = 0; i < count; ++i) {
        m_grid->addWidget(m_viewers[i], i / cols, i % cols);
        m_viewers[i]->show();
    }

    for (int i = count; i < totalCells; ++i) {
        auto *label = new QLabel("No Stream", m_gridWidget);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("color: #444; font-size: 13px; background-color: #111;");
        m_emptyLabels.append(label);
        m_grid->addWidget(label, i / cols, i % cols);
    }

    updateGridCellSizes();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_gridWidget && event->type() == QEvent::Resize) {
        updateGridCellSizes();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::updateGridCellSizes()
{
    int count = m_viewers.size();
    if (count == 0 || !m_grid) return;

    int cols = effectiveGridCols();
    int gridWidth = m_gridWidget->parentWidget() ? m_gridWidget->parentWidget()->width() : m_gridWidget->width();
    int cellWidth = (gridWidth - (cols - 1)) / cols;
    int infoBarHeight = 36;
    int cellHeight = cellWidth * 3 / 4 + infoBarHeight;

    for (auto *viewer : m_viewers) {
        viewer->setFixedHeight(cellHeight);
    }
    for (auto *label : m_emptyLabels) {
        label->setFixedHeight(cellHeight);
    }
}
