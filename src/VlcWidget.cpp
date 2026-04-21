#include "VlcWidget.h"
#include "Style.h"
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

    // 재접속 타이머 (singleShot — 겹침 방지)
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
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
    m_reconnecting = false;
    m_reconnectCount = 0;

    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    if (!media) {
        log(QString("[%1] Failed to create media for %2").arg(m_name, url), 3);
        setStatus(Status::Failed);
        showStatus("Invalid URL");
        return;
    }
    libvlc_media_add_option(media, ":rtsp-tcp");

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (!m_player) {
        log(QString("[%1] Failed to create player").arg(m_name), 3);
        setStatus(Status::Failed);
        showStatus("Player Error");
        return;
    }

    attachToSurface();

    m_placeholder->hide();
    m_videoSurface->show();

    m_nameLabel->setText(name);
    m_infoLabel->setText(url);
    m_nameLabel->show();
    m_infoLabel->show();
    m_timeLabel->show();

    setupEvents();
    libvlc_media_player_play(m_player);
    setStatus(Status::Connecting);
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

void VlcWidget::detachEvents()
{
    if (!m_player) return;

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_player);
    libvlc_event_detach(em, libvlc_MediaPlayerEndReached, onEndReached, this);
    libvlc_event_detach(em, libvlc_MediaPlayerEncounteredError, onError, this);
    libvlc_event_detach(em, libvlc_MediaPlayerPlaying, onPlaying, this);
}

void VlcWidget::cleanupPlayer()
{
    if (!m_player) return;

    detachEvents();

    auto *player = m_player;
    m_player = nullptr;
    // 비동기로 정리 (UI 블로킹 방지)
    (void)QtConcurrent::run([player]() {
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);
    });
}

void VlcWidget::setStatus(Status s)
{
    m_status = s;
    emit statusChanged(this, s);
}

QString VlcWidget::statusText(Status s)
{
    switch (s) {
        case Status::Idle:          return "대기";
        case Status::Connecting:    return "연결중";
        case Status::Connected:     return "연결됨";
        case Status::Disconnected:  return "끊김";
        case Status::Reconnecting:  return "재연결중";
        case Status::Failed:        return "실패";
    }
    return "";
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
        self->m_reconnecting = false;
        self->m_reconnectCount = 0;
        self->m_reconnectTimer->stop();
        self->setStatus(Status::Connected);
        self->m_placeholder->hide();
        self->m_videoSurface->show();
        self->log(QString("[%1] Stream connected").arg(self->m_name), 1);
    }, Qt::QueuedConnection);
}

void VlcWidget::onEndReached(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        if (!self->m_reconnecting) {
            self->m_reconnectCount = 0;
        }
        self->m_reconnecting = true;
        self->setStatus(Status::Disconnected);
        self->showStatus("끊김");
        self->log(QString("[%1] Disconnected").arg(self->m_name), 2);
        if (self->m_autoReconnect && !self->m_reconnectTimer->isActive()) {
            self->m_reconnectTimer->start();
        }
    }, Qt::QueuedConnection);
}

void VlcWidget::onError(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        if (!self->m_reconnecting) {
            self->m_reconnectCount = 0;
        }
        self->m_reconnecting = true;
        self->setStatus(Status::Disconnected);
        self->showStatus("연결 오류");
        self->log(QString("[%1] Connection Error").arg(self->m_name), 3);
        if (self->m_autoReconnect && !self->m_reconnectTimer->isActive()) {
            self->m_reconnectTimer->start();
        }
    }, Qt::QueuedConnection);
}

void VlcWidget::tryReconnect()
{
    m_reconnectCount++;

    if (m_url.isEmpty() || m_reconnectCount > MAX_RECONNECT) {
        m_reconnectTimer->stop();
        m_reconnecting = false;
        if (m_reconnectCount > MAX_RECONNECT) {
            setStatus(Status::Failed);
            showStatus("Connection Failed");
            log(QString("[%1] Max reconnect attempts reached").arg(m_name), 3);
        }
        return;
    }

    // 이전 플레이어 정리
    cleanupPlayer();

    setStatus(Status::Reconnecting);
    showStatus(QString("재연결중... (%1/%2)").arg(m_reconnectCount).arg(MAX_RECONNECT));
    log(QString("[%1] Reconnecting... attempt %2").arg(m_name).arg(m_reconnectCount), 2);

    // 재접속 시도
    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, m_url.toUtf8().constData());
    if (!media) {
        log(QString("[%1] Failed to create media").arg(m_name), 3);
        m_reconnectTimer->start();  // 다음 재시도 예약
        return;
    }
    libvlc_media_add_option(media, ":rtsp-tcp");

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (!m_player) {
        log(QString("[%1] Failed to create player").arg(m_name), 3);
        m_reconnectTimer->start();  // 다음 재시도 예약
        return;
    }

    attachToSurface();

    // placeholder를 유지 — 연결 성공(onPlaying) 시에만 영상 표면 표시
    setupEvents();
    libvlc_media_player_play(m_player);
    // 타이머는 여기서 시작하지 않음 — onError/onEndReached에서 실패 시 다시 예약
}

void VlcWidget::reconnect()
{
    if (m_url.isEmpty()) return;

    QString url = m_url;
    QString name = m_name;

    cleanupPlayer();
    m_reconnectTimer->stop();
    m_reconnecting = false;
    m_reconnectCount = 0;

    play(url, name);
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
    m_reconnecting = false;

    cleanupPlayer();

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

    libvlc_media_t *media = libvlc_media_player_get_media(m_player);
    if (media) {
        libvlc_media_stats_t stats{};
        if (libvlc_media_get_stats(media, &stats)) {
            // f_demux_bitrate 단위: bytes/µs → × 8000 = kbit/s
            s.bitrate_kbps = static_cast<double>(stats.f_demux_bitrate) * 8000.0;
            s.displayed_pictures = stats.i_displayed_pictures;
            s.lost_pictures = stats.i_lost_pictures;
        }

        // FPS: 비디오 트랙에서 프레임레이트 조회 (get_fps 대체)
        libvlc_media_track_t **tracks = nullptr;
        unsigned trackCount = libvlc_media_tracks_get(media, &tracks);
        for (unsigned i = 0; i < trackCount; ++i) {
            if (tracks[i]->i_type == libvlc_track_video && tracks[i]->video
                && tracks[i]->video->i_frame_rate_den > 0) {
                s.fps = static_cast<double>(tracks[i]->video->i_frame_rate_num)
                      / tracks[i]->video->i_frame_rate_den;
                break;
            }
        }
        if (tracks) libvlc_media_tracks_release(tracks, trackCount);

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
    menu.setStyleSheet(Style::MENU);

    auto *fullscreenAction = menu.addAction("전체화면으로 열기");
    auto *snapshotAction = menu.addAction("스냅샷 저장");
    auto *reconnectAction = menu.addAction("재연결");
    menu.addSeparator();
    auto *removeAction = menu.addAction("채널 삭제");

    fullscreenAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);
    snapshotAction->setEnabled(isPlaying());
    reconnectAction->setEnabled(!m_url.isEmpty() && m_status != Status::Connected && m_status != Status::Connecting);
    removeAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);

    auto *selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == fullscreenAction) {
        emit requestFullscreen(this);
    } else if (selected == reconnectAction) {
        reconnect();
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
    if (!m_player) return;

#if defined(__APPLE__)
    libvlc_media_player_set_nsobject(m_player, (void *)m_videoSurface->winId());
#elif defined(_WIN32)
    libvlc_media_player_set_hwnd(m_player, (void *)m_videoSurface->winId());
#else
    libvlc_media_player_set_xwindow(m_player, m_videoSurface->winId());
#endif
}
