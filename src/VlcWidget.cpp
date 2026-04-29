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
#include <QFileInfo>
#include <QFileDialog>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QScreen>
#include <QPixmap>
#include <QGuiApplication>
#include <atomic>

namespace {

std::atomic<quint64> g_recordingSeq{0};

QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024.0) return QString("%1 KB").arg(kb, 0, 'f', 1);
    double mb = kb / 1024.0;
    if (mb < 1024.0) return QString("%1 MB").arg(mb, 0, 'f', 1);
    return QString("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
}

QString formatElapsed(int seconds) {
    int h = seconds / 3600;
    int m = (seconds / 60) % 60;
    int s = seconds % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

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

    m_recBadge = new QLabel(this);
    m_recBadge->setStyleSheet(Style::REC_BADGE_ACTIVE);
    m_recBadge->hide();

    m_snapshotBtn = new QPushButton("📷", this);
    m_snapshotBtn->setFixedSize(24, 20);
    m_snapshotBtn->setCursor(Qt::PointingHandCursor);
    m_snapshotBtn->setToolTip("스냅샷 저장");
    m_snapshotBtn->setStyleSheet(Style::TOOL_BUTTON);
    connect(m_snapshotBtn, &QPushButton::clicked, this, &VlcWidget::saveSnapshot);

    m_recordBtn = new QPushButton("●", this);
    m_recordBtn->setFixedSize(24, 20);
    m_recordBtn->setCursor(Qt::PointingHandCursor);
    m_recordBtn->setToolTip("녹화 시작");
    m_recordBtn->setStyleSheet(Style::TOOL_BUTTON);
    connect(m_recordBtn, &QPushButton::clicked, this, [this]() {
        VlcWidget *t = recordingTarget();
        if (!t) return;
        if (t->m_recState == RecState::Idle) t->startRecording();
        else if (t->m_recState == RecState::Active) t->stopRecording();
        // Starting 상태에선 버튼이 disabled (updateRecordUi)
    });

    auto *row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->setSpacing(0);
    row1->addWidget(m_nameLabel);
    row1->addStretch();
    row1->addWidget(m_recBadge);
    row1->addWidget(m_snapshotBtn);
    row1->addWidget(m_recordBtn);
    row1->addSpacing(4);
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

    updateRecordUi();

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

// ============================================================
// media 생성 공통 헬퍼 (녹화 옵션 포함)
// ============================================================

libvlc_media_t *VlcWidget::openMedia(const QString &url, bool withRecording)
{
    auto *media = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    if (!media) return nullptr;
    libvlc_media_add_option(media, ":rtsp-tcp");
    if (withRecording && !m_recordingPath.isEmpty()) {
        QString sout = QString(":sout=#duplicate{dst=display,"
                               "dst=std{access=file,mux=mkv,dst=\"%1\"}}")
                        .arg(formatSoutPath(m_recordingPath));
        libvlc_media_add_option(media, sout.toUtf8().constData());
        libvlc_media_add_option(media, ":sout-keep");
    }
    return media;
}

void VlcWidget::play(const QString &url, const QString &name)
{
    stop();

    m_url = url;
    m_name = name;
    m_reconnecting = false;
    m_reconnectCount = 0;

    libvlc_media_t *media = openMedia(url, /*withRecording=*/false);
    if (!media) {
        log(QString("[%1] Failed to create media for %2").arg(m_name, url), 3);
        setStatus(Status::Failed);
        showStatus("Invalid URL");
        return;
    }

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

    updateRecordUi();
}

void VlcWidget::setChannelInfo(const QString &name, const QString &url)
{
    m_name = name;
    m_url = url;
    m_nameLabel->setText(name);
    m_infoLabel->setText(url);
    updateRecordUi();
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

QFuture<void> VlcWidget::cleanupPlayer()
{
    if (!m_player) return QtFuture::makeReadyVoidFuture();

    detachEvents();

    auto *player = m_player;
    m_player = nullptr;
    // 비동기로 정리 (UI 블로킹 방지). future 는 호출자가 필요하면 대기.
    return QtConcurrent::run([player]() {
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

        // fps 캐시: 연결 성공 시점 1회만 비디오 트랙에서 조회
        self->m_cachedFps = 0.0;
        if (self->m_player) {
            libvlc_media_t *media = libvlc_media_player_get_media(self->m_player);
            if (media) {
                libvlc_media_track_t **tracks = nullptr;
                unsigned n = libvlc_media_tracks_get(media, &tracks);
                for (unsigned i = 0; i < n; ++i) {
                    if (tracks[i]->i_type == libvlc_track_video && tracks[i]->video
                        && tracks[i]->video->i_frame_rate_den > 0) {
                        self->m_cachedFps =
                            static_cast<double>(tracks[i]->video->i_frame_rate_num)
                          / tracks[i]->video->i_frame_rate_den;
                        break;
                    }
                }
                if (tracks) libvlc_media_tracks_release(tracks, n);
                libvlc_media_release(media);
            }
        }

        self->log(QString("[%1] Stream connected").arg(self->m_name), 1);
    }, Qt::QueuedConnection);
}

void VlcWidget::onEndReached(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        // 녹화 상태라면 Disconnect 경로로 녹화 마무리
        if (self->m_recState == RecState::Active) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
        } else if (self->m_recState == RecState::Starting) {
            const QString p = self->m_recordingPath;
            self->m_recordingPath.clear();
            self->setRecState(RecState::Idle);
            emit self->recordingFailed(self, p, "disconnected during startup");
        }

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
        if (self->m_recState == RecState::Active) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
        } else if (self->m_recState == RecState::Starting) {
            const QString p = self->m_recordingPath;
            self->m_recordingPath.clear();
            self->setRecState(RecState::Idle);
            emit self->recordingFailed(self, p, "disconnected during startup");
        }

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

    // 재접속 시엔 녹화 걸지 않음 (끊김 시 자동 중단 정책)
    libvlc_media_t *media = openMedia(m_url, /*withRecording=*/false);
    if (!media) {
        log(QString("[%1] Failed to create media").arg(m_name), 3);
        m_reconnectTimer->start();
        return;
    }

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (!m_player) {
        log(QString("[%1] Failed to create player").arg(m_name), 3);
        m_reconnectTimer->start();
        return;
    }

    attachToSurface();
    setupEvents();
    libvlc_media_player_play(m_player);
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
    updateRecordUi();
}

void VlcWidget::stop()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    m_reconnecting = false;

    // 녹화 중이었다면 Remove 의미로 마무리 (재생 복구 스킵, flush 만 비동기)
    if (m_recState != RecState::Idle) {
        finalizeRecordingForShutdown();
    }

    cleanupPlayer();  // m_player 가 이미 null 이면 no-op

    m_url.clear();
    m_name.clear();
    m_videoSurface->hide();
    m_nameLabel->hide();
    m_infoLabel->hide();
    m_timeLabel->hide();
    m_placeholder->show();

    updateRecordUi();
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
    s.fps = m_cachedFps;  // onPlaying 시점에 1회 계산된 값

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
    auto *editAction = menu.addAction("채널 수정");
    auto *snapshotAction = menu.addAction("스냅샷 저장");
    VlcWidget *t = recordingTarget();
    const bool rec = t && t->m_recState == RecState::Active;
    const bool starting = t && t->m_recState == RecState::Starting;
    auto *recordAction = menu.addAction(rec ? "녹화 중지" : "녹화 시작");
    menu.addSeparator();
    auto *reconnectAction = menu.addAction("재연결");
    menu.addSeparator();
    auto *removeAction = menu.addAction("채널 삭제");

    fullscreenAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);
    editAction->setEnabled(!m_url.isEmpty() && (!m_isFullscreen || m_sourceViewer));
    snapshotAction->setEnabled(isPlaying());
    recordAction->setEnabled(t && !starting && (rec || t->isPlaying()));
    reconnectAction->setEnabled(!m_url.isEmpty() && m_status != Status::Connected && m_status != Status::Connecting);
    removeAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);

    auto *selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == fullscreenAction) {
        emit requestFullscreen(this);
    } else if (selected == editAction) {
        emit requestEdit(m_isFullscreen && m_sourceViewer ? m_sourceViewer.data() : this);
    } else if (selected == reconnectAction) {
        reconnect();
    } else if (selected == snapshotAction) {
        saveSnapshot();
    } else if (selected == recordAction) {
        if (!t) return;
        if (t->m_recState == RecState::Active) t->stopRecording();
        else if (t->m_recState == RecState::Idle) t->startRecording();
    } else if (selected == removeAction) {
        emit requestRemove(this);
    }
}

void VlcWidget::saveSnapshot()
{
    if (!isPlaying()) return;
    QString dir = QDir::homePath() + "/.ziilab/snapshots";
    QDir().mkpath(dir);
    QString filename = QString("%1/%2_%3.png")
        .arg(dir, m_name.isEmpty() ? "snapshot" : safeFileStem(m_name),
             QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    if (takeSnapshot(filename)) {
        log(QString("[%1] Snapshot saved: %2").arg(m_name, filename), 1);
        emit snapshotTaken(filename);
    } else {
        log(QString("[%1] Snapshot failed").arg(m_name), 3);
    }
}

bool VlcWidget::takeSnapshot(const QString &filePath)
{
    if (!m_player || !isPlaying()) return false;

    unsigned int width = 0, height = 0;
    libvlc_video_get_size(m_player, 0, &width, &height);
    if (width == 0) width = 640;
    if (height == 0) height = 480;

    // 1차: libVLC 내장 snapshot
    if (libvlc_video_take_snapshot(m_player, 0,
            filePath.toUtf8().constData(), width, height) == 0) {
        return true;
    }

    // 2차 폴백: sout 파이프라인이 활성이면 libVLC snapshot 이 실패할 수 있음 (3.x 제약).
    // QScreen 으로 네이티브 비디오 서피스 영역을 OS-level 스크린 그랩.
    QScreen *screen = m_videoSurface->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return false;

    const WId wid = m_videoSurface->winId();
    QPixmap pm = screen->grabWindow(wid, 0, 0, -1, -1);
    if (pm.isNull()) return false;
    return pm.save(filePath, "PNG");
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

// ============================================================
// 녹화 — 경로/sanitize 유틸
// ============================================================

QString VlcWidget::safeFileStem(const QString &name) const
{
    static const QRegularExpression bad(R"([<>:"/\\|?*'{}\[\],])");
    QString s = name;
    s.replace(bad, "_");
    return s.trimmed().isEmpty() ? QStringLiteral("channel") : s;
}

QString VlcWidget::formatSoutPath(const QString &absolutePath) const
{
    QString p = absolutePath;
#ifdef Q_OS_WIN
    p.replace('\\', '/');  // VLC 는 Windows 에서도 forward slash 허용
#endif
    p.replace('"', '_');    // double-quote 방어
    return p;
}

QString VlcWidget::makeRecordingPath() const
{
    const QString dir = QDir::homePath() + "/.ziilab/recordings";
    QDir().mkpath(dir);
    const QString stem = safeFileStem(m_name);
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const quint64 seq = ++g_recordingSeq;

    for (int i = 0; i < 100; ++i) {
        const QString suffix = (i == 0) ? QString() : QString("_%1").arg(i);
        const QString path = QString("%1/%2_%3_%4%5.mkv")
            .arg(dir, stem, ts).arg(seq).arg(suffix);
        if (!QFileInfo::exists(path)) return path;
    }
    return QString("%1/%2_%3_%4.mkv").arg(dir, stem, ts).arg(seq);
}

// ============================================================
// 녹화 — 상태/리모트 제어
// ============================================================

VlcWidget *VlcWidget::recordingTarget()
{
    if (m_isFullscreen) {
        return m_sourceViewer.data();
    }
    return this;
}

void VlcWidget::setRecState(RecState s)
{
    if (m_recState == s) return;
    m_recState = s;
    emit recordingStateChanged(this, s);
    updateRecordUi();
}

void VlcWidget::fallbackToReconnect()
{
    log(QString("[%1] Playback recovery failed; falling back to reconnect").arg(m_name), 3);
    setStatus(Status::Disconnected);
    showStatus("재연결 대기");
    if (m_autoReconnect && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

bool VlcWidget::restartPlayerPreserving(bool withRecording)
{
    QString url = m_url;
    cleanupPlayer();  // future 는 여기선 미사용
    libvlc_media_t *media = openMedia(url, withRecording);
    if (!media) return false;
    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);
    if (!m_player) return false;
    attachToSurface();
    setupEvents();
    libvlc_media_player_play(m_player);
    setStatus(Status::Connecting);
    return true;
}

bool VlcWidget::openAndPlayNoRecording()
{
    libvlc_media_t *media = openMedia(m_url, /*withRecording=*/false);
    if (!media) return false;
    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);
    if (!m_player) return false;
    attachToSurface();
    setupEvents();
    libvlc_media_player_play(m_player);
    setStatus(Status::Connecting);
    return true;
}

// ============================================================
// 녹화 — 시작
// ============================================================

void VlcWidget::startRecording()
{
    VlcWidget *t = recordingTarget();
    if (t != this) { t->startRecording(); return; }

    if (m_recState != RecState::Idle || !isPlaying() || m_url.isEmpty()) return;

    const QString path = makeRecordingPath();
    m_recordingPath = path;
    m_recordingStartTime = QDateTime::currentDateTime();

    if (!restartPlayerPreserving(/*withRecording=*/true)) {
        m_recordingPath.clear();
        emit recordingFailed(this, path, "media/player creation failed");
        log(QString("[%1] Recording failed (creation): %2").arg(m_name, path), 3);

        if (!restartPlayerPreserving(false)) {
            fallbackToReconnect();
        }
        return;
    }

    setRecState(RecState::Starting);
    startRecordingWatchdog(path);
}

void VlcWidget::startRecordingWatchdog(const QString &path)
{
    m_recWatchdogAttempts = 0;
    scheduleWatchdogTick(path);
}

void VlcWidget::scheduleWatchdogTick(const QString &path)
{
    QTimer::singleShot(500, this, [this, path]() {
        // 상태가 바뀌었다면 중단
        if (m_recState != RecState::Starting || m_recordingPath != path) return;

        QFileInfo fi(path);
        if (fi.exists() && fi.size() > 0) {
            setRecState(RecState::Active);
            log(QString("[%1] Recording started: %2").arg(m_name, path), 1);
            emit recordingStarted(this, path);
            return;
        }

        if (++m_recWatchdogAttempts >= 10) {
            // 5초 타임아웃
            const QString reason = fi.exists() ? "no data written in 5s" : "file not created in 5s";
            m_recordingPath.clear();
            setRecState(RecState::Idle);
            log(QString("[%1] Recording failed: %2 (%3)").arg(m_name, path, reason), 3);
            emit recordingFailed(this, path, reason);

            if (!restartPlayerPreserving(false)) {
                fallbackToReconnect();
            }
            return;
        }

        scheduleWatchdogTick(path);
    });
}

// ============================================================
// 녹화 — 중지
// ============================================================

void VlcWidget::stopRecording()
{
    VlcWidget *t = recordingTarget();
    if (t != this) { t->stopRecording(); return; }
    if (m_recState == RecState::Idle) return;
    stopRecordingInternal(RecStopReason::Manual);
}

void VlcWidget::finalizeRecordingForShutdown()
{
    if (m_recState == RecState::Idle) return;
    stopRecordingInternal(RecStopReason::Remove);
}

void VlcWidget::stopRecordingInternal(RecStopReason reason)
{
    if (m_recState == RecState::Idle) return;

    const QString path = m_recordingPath;
    const QDateTime start = m_recordingStartTime;
    const QDateTime stoppedAt = QDateTime::currentDateTime();
    const bool wasActive = (m_recState == RecState::Active);
    const bool byDisc = (reason == RecStopReason::Disconnect);
    const bool isRemove = (reason == RecStopReason::Remove);

    m_recordingPath.clear();
    setRecState(RecState::Idle);

    // 현재 녹화중 플레이어 cleanup → 파일 finalizer 완료를 future 로 추적
    QFuture<void> flushFuture = cleanupPlayer();

    // 재생 복구는 Manual 일 때만
    if (reason == RecStopReason::Manual) {
        if (!openAndPlayNoRecording()) {
            fallbackToReconnect();
        }
    }
    // Disconnect: onEnd/onError 가 setStatus(Disconnected) + reconnect 타이머 처리
    // Remove: 재생 복구 안 함

    if (wasActive) {
        auto *watcher = new QFutureWatcher<void>(this);
        connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, path, start, stoppedAt, byDisc, watcher]() {
                qint64 bytes = QFileInfo(path).size();
                int dur = start.secsTo(stoppedAt);
                if (dur < 0) dur = 0;
                log(QString("[%1] Recording stopped%2: %3 (%4, %5s)")
                    .arg(m_name, byDisc ? " (disconnect)" : "", path, humanSize(bytes))
                    .arg(dur), 1);
                emit recordingStopped(this, path, bytes, dur, byDisc);
                watcher->deleteLater();
            });
        watcher->setFuture(flushFuture);
    } else if (!isRemove) {
        // Starting 이었다가 Disconnect/Manual 로 내려간 경우 → Failed
        emit recordingFailed(this, path,
            byDisc ? "disconnected during startup" : "stopped during startup");
    }
}

// ============================================================
// 녹화 — UI 갱신
// ============================================================

void VlcWidget::updateRecordUi()
{
    VlcWidget *t = recordingTarget();
    const RecState s = t ? t->m_recState : RecState::Idle;
    const int sAsInt = static_cast<int>(s);
    const bool stateChanged = (sAsInt != m_lastDisplayedRec);

    switch (s) {
        case RecState::Idle:
            if (stateChanged) {
                m_recBadge->hide();
                m_recordBtn->setText("●");
                m_recordBtn->setToolTip("녹화 시작");
                m_recordBtn->setStyleSheet(Style::TOOL_BUTTON);
            }
            m_recordBtn->setEnabled(t && t->isPlaying());
            break;
        case RecState::Starting:
            if (stateChanged) {
                m_recBadge->setStyleSheet(Style::REC_BADGE_STARTING);
                m_recBadge->setText("● REC…");
                m_recBadge->show();
                m_recordBtn->setText("●");
                m_recordBtn->setToolTip("녹화 시작 중…");
                m_recordBtn->setStyleSheet(Style::TOOL_BUTTON);
                m_recordBtn->setEnabled(false);
            }
            break;
        case RecState::Active: {
            if (stateChanged) {
                m_recBadge->setStyleSheet(Style::REC_BADGE_ACTIVE);
                m_recBadge->show();
                m_recordBtn->setText("■");
                m_recordBtn->setToolTip("녹화 중지");
                m_recordBtn->setStyleSheet(Style::TOOL_BUTTON_REC);
                m_recordBtn->setEnabled(true);
            }
            // 경과시간은 매 틱 갱신 (텍스트만, 스타일 재적용 없음)
            int elapsed = t->m_recordingStartTime.secsTo(QDateTime::currentDateTime());
            if (elapsed < 0) elapsed = 0;
            m_recBadge->setText(QString("● REC %1").arg(formatElapsed(elapsed)));
            break;
        }
    }

    m_lastDisplayedRec = sAsInt;
    m_snapshotBtn->setEnabled(isPlaying());
}
