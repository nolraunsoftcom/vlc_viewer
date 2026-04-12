#include "VlcWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QDateTime>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDir>
#include <QFileDialog>
#include <QtConcurrent>

VlcWidget::VlcWidget(libvlc_instance_t *vlcInstance, QWidget *parent)
    : QWidget(parent)
    , m_vlcInstance(vlcInstance)
{

    // 상단 정보 바 (2줄)
    // 1행: [이름]                    [시간]
    // 2행: [URL]
    auto *infoBar = new QWidget(this);
    infoBar->setStyleSheet("background-color: rgba(0,0,0,180);");
    infoBar->setFixedHeight(36);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("color: white; font-size: 11px; font-weight: bold;");

    m_timeLabel = new QLabel(this);
    m_timeLabel->setStyleSheet("color: #ccc; font-size: 10px;");

    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet("color: #888; font-size: 10px;");

    auto *row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(0);
    row1->addWidget(m_nameLabel);
    row1->addStretch();
    row1->addWidget(m_timeLabel);

    auto *barLayout = new QVBoxLayout(infoBar);
    barLayout->setContentsMargins(6, 2, 6, 2);
    barLayout->setSpacing(0);
    barLayout->addLayout(row1);
    barLayout->addWidget(m_infoLabel);

    // 비디오가 그려질 표면
    m_videoSurface = new QWidget(this);
    m_videoSurface->setStyleSheet("background-color: black;");

    // 연결 전 안내 텍스트
    m_placeholder = new QLabel("No Stream", this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet("color: #666; font-size: 14px; background-color: black;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(infoBar);
    layout->addWidget(m_videoSurface, 1);
    layout->addWidget(m_placeholder, 1);

    m_videoSurface->hide();
    m_nameLabel->hide();
    m_infoLabel->hide();
    m_timeLabel->hide();
    m_placeholder->show();

    // 재접속 타이머 (5초 간격)
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(5000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &VlcWidget::tryReconnect);
}

VlcWidget::~VlcWidget()
{
    stop();
}

void VlcWidget::play(const QString &url, const QString &name)
{
    stop();

    m_url = url;
    m_name = name;

    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    libvlc_media_add_option(media, ":rtsp-tcp");

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    attachToSurface();

    m_placeholder->hide();
    m_videoSurface->show();

    // 상단 정보 표시
    m_nameLabel->setText(name);
    m_infoLabel->setText(url);
    m_nameLabel->show();
    m_infoLabel->show();
    m_timeLabel->show();

    setupEvents();
    libvlc_media_player_play(m_player);
    log(QString("[%1] Connecting to %2").arg(m_name, url), 0);
}

void VlcWidget::setupEvents()
{
    if (!m_player) return;

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_player);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, onEndReached, this);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, onError, this);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, onPlaying, this);
}

void VlcWidget::showStatus(const QString &text)
{
    m_videoSurface->hide();
    m_placeholder->setText(text);
    m_placeholder->show();
}

void VlcWidget::log(const QString &msg, int level)
{
    if (m_logCallback) m_logCallback(msg, level);
}

void VlcWidget::onPlaying(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        self->log(QString("[%1] Stream connected").arg(self->m_name), 1);
    }, Qt::QueuedConnection);
}

void VlcWidget::onEndReached(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        self->showStatus("Disconnected — Reconnecting...");
        self->log(QString("[%1] Disconnected").arg(self->m_name), 2);
        self->m_reconnectCount = 0;
        self->m_reconnectTimer->start();
    }, Qt::QueuedConnection);
}

void VlcWidget::onError(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        self->showStatus("Connection Error — Reconnecting...");
        self->log(QString("[%1] Connection Error").arg(self->m_name), 3);
        self->m_reconnectCount = 0;
        self->m_reconnectTimer->start();
    }, Qt::QueuedConnection);
}

void VlcWidget::tryReconnect()
{
    m_reconnectCount++;

    if (m_url.isEmpty()) {
        m_reconnectTimer->stop();
        return;
    }

    // 이전 플레이어 정리
    if (m_player) {
        libvlc_media_player_stop(m_player);
        libvlc_media_player_release(m_player);
        m_player = nullptr;
    }

    showStatus(QString("Reconnecting... (%1)").arg(m_reconnectCount));
    log(QString("[%1] Reconnecting... attempt %2").arg(m_name).arg(m_reconnectCount), 2);

    // 재접속 시도
    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, m_url.toUtf8().constData());
    libvlc_media_add_option(media, ":rtsp-tcp");

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    attachToSurface();

    m_placeholder->hide();
    m_videoSurface->show();

    setupEvents();
    libvlc_media_player_play(m_player);

    m_reconnectTimer->stop();
}

void VlcWidget::updateTime(const QString &timeStr)
{
    m_timeLabel->setText(timeStr);
}

void VlcWidget::stop()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }

    if (m_player) {
        // 비동기로 정리 (UI 블로킹 방지)
        auto *player = m_player;
        m_player = nullptr;
        QtConcurrent::run([player]() {
            libvlc_media_player_stop(player);
            libvlc_media_player_release(player);
        });
    }

    m_url.clear();
    m_name.clear();
    m_videoSurface->hide();
    m_nameLabel->hide();
    m_infoLabel->hide();
    m_timeLabel->hide();
    m_placeholder->show();
}

bool VlcWidget::isPlaying() const
{
    if (!m_player) return false;
    return libvlc_media_player_is_playing(m_player);
}

VlcWidget::Stats VlcWidget::getStats() const
{
    Stats s{};
    s.playing = isPlaying();

    if (!m_player || !s.playing) return s;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    s.fps = static_cast<double>(libvlc_media_player_get_fps(m_player));
#pragma clang diagnostic pop

    libvlc_media_t *media = libvlc_media_player_get_media(m_player);
    if (media) {
        libvlc_media_stats_t stats{};
        if (libvlc_media_get_stats(media, &stats)) {
            // f_demux_bitrate is in MiB/s, convert to Kbps
            s.bitrate_kbps = static_cast<double>(stats.f_demux_bitrate) * 8.0 * 1024.0;
            s.displayed_pictures = stats.i_displayed_pictures;
            s.lost_pictures = stats.i_lost_pictures;
        }
        libvlc_media_release(media);
    }

    return s;
}

void VlcWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    emit doubleClicked(this);
}

void VlcWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showContextMenu(event->globalPosition().toPoint());
        return;
    }
    QWidget::mousePressEvent(event);
}

void VlcWidget::showContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2a2a2a; color: #ccc; border: 1px solid #444; font-size: 12px; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background-color: #335; }");

    auto *fullscreenAction = menu.addAction("전체화면으로 열기");
    auto *snapshotAction = menu.addAction("스냅샷 저장");
    menu.addSeparator();
    auto *removeAction = menu.addAction("채널 삭제");
    removeAction->setEnabled(!m_url.isEmpty());

    auto *selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == fullscreenAction) {
        emit requestFullscreen(this);
    } else if (selected == snapshotAction) {
        QString dir = QDir::homePath() + "/.ziilab/snapshots";
        QDir().mkpath(dir);
        QString filename = QString("%1/%2_%3.png")
            .arg(dir, m_name.isEmpty() ? "snapshot" : m_name,
                 QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        if (takeSnapshot(filename)) {
            log(QString("[%1] Snapshot saved: %2").arg(m_name, filename), 1);
            emit snapshotTaken(filename);
        } else {
            log(QString("[%1] Snapshot failed").arg(m_name), 3);
        }
    } else if (selected == removeAction) {
        emit requestRemove(this);
    }
}

bool VlcWidget::takeSnapshot(const QString &filePath)
{
    if (!m_player || !isPlaying()) return false;

    unsigned int width = 0, height = 0;
    libvlc_video_get_size(m_player, 0, &width, &height);
    if (width == 0) width = 640;
    if (height == 0) height = 480;

    return libvlc_video_take_snapshot(m_player, 0,
        filePath.toUtf8().constData(), width, height) == 0;
}

void VlcWidget::attachToSurface()
{
#if defined(__APPLE__)
    libvlc_media_player_set_nsobject(m_player, (void *)m_videoSurface->winId());
#elif defined(_WIN32)
    libvlc_media_player_set_hwnd(m_player, (void *)m_videoSurface->winId());
#else
    libvlc_media_player_set_xwindow(m_player, m_videoSurface->winId());
#endif
}
