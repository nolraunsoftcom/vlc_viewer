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
#include <QUrl>
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

bool isLocalRtspUrl(const QString &url)
{
    const QUrl parsed(url);
    const QString host = parsed.host().toLower();
    return host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("localhost")
        || host == QStringLiteral("::1");
}

} // namespace

VlcWidget::VlcWidget(libvlc_instance_t *vlcInstance, QWidget *parent)
    : QWidget(parent)
    , m_vlcInstance(vlcInstance)
{
    // 상단 정보 바
    auto *infoBar = new QWidget(this);
    infoBar->setStyleSheet("background-color: #f3f3f3; border-bottom: 1px solid #d0d0d0;");
    infoBar->setFixedHeight(28);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("color: #222; font-size: 11px; font-weight: bold;");

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

    auto *barLayout = new QVBoxLayout(infoBar);
    barLayout->setContentsMargins(6, 4, 6, 4);
    barLayout->setSpacing(0);
    barLayout->addLayout(row1);

    // 비디오가 그려질 표면
    m_videoSurface = new QWidget(this);
    m_videoSurface->setStyleSheet("background-color: black;");

    // 연결 전 안내 텍스트
    m_placeholder = new QLabel("No Stream", this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet("color: #777; font-size: 14px; background-color: #ededed;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(infoBar);
    layout->addWidget(m_videoSurface, 1);
    layout->addWidget(m_placeholder, 1);

    m_videoSurface->hide();
    m_nameLabel->hide();
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
    waitForCleanupFinished();
}

// ============================================================
// media 생성 헬퍼
// ============================================================

libvlc_media_t *VlcWidget::openPlaybackMedia(const QString &url)
{
    auto *media = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    if (!media) return nullptr;
    // 의견#2: UDP 사용 (:rtsp-tcp 미지정 = VLC 순정 기본값 UDP) — 음영지역 재연결 복구 단축.
    // TCP 로 되돌리려면 아래에 libvlc_media_add_option(media, ":rtsp-tcp"); 한 줄 추가.
    if (isLocalRtspUrl(url)) {
        libvlc_media_add_option(media, ":rtsp-tcp");
    }
    libvlc_media_add_option(media, ":network-caching=1000");  // 의견#5
    libvlc_media_add_option(media, ":live-caching=1000");     // (v0.0.3) 1500 되돌림 — 버퍼는 뭉개짐 원인 아니었음
    // 디코드 견고화: HW 가속 명시 + 임의 프레임 스킵 금지.
    // (v0.0.3) hurry-up 비활성화 — 우리 앱(2스트림 + Qt 임베드 렌더)은 plain VLC 보다 쉽게 부하에
    // 걸리고, 기본 ON 인 hurry-up 이 부하 시 프레임을 부분디코드/왜곡(뭉개짐)한다. 끄면 왜곡 대신
    // (늦은 프레임은) drop-late 로 처리 → 뭉개짐↓, 대신 부하 시 약간의 끊김 가능.
    libvlc_media_add_option(media, ":avcodec-hw=any");
    libvlc_media_add_option(media, ":avcodec-skip-frame=0");
    libvlc_media_add_option(media, ":no-avcodec-hurry-up");
    libvlc_media_add_option(media, ":no-audio");
    libvlc_media_add_option(media, ":audio-track=-1");
    return media;
}

libvlc_media_t *VlcWidget::openRecordingMedia(const QString &url, const QString &path)
{
    auto *media = libvlc_media_new_location(m_vlcInstance, url.toUtf8().constData());
    if (!media) return nullptr;
    // 녹화도 재생과 동일하게 UDP 기본값을 사용한다.
    // 무선 구간에서 일부 패킷 손실이 있더라도 TCP 재전송 지연/teardown 지연보다
    // 최신 스트림을 계속 기록하는 쪽을 우선한다.
    if (isLocalRtspUrl(url)) {
        libvlc_media_add_option(media, ":rtsp-tcp");
    }
    libvlc_media_add_option(media, ":network-caching=1000");
    libvlc_media_add_option(media, ":live-caching=1000");
    libvlc_media_add_option(media, ":no-audio");
    libvlc_media_add_option(media, ":audio-track=-1");

    const QString sout = QString(":sout=#std{access=file,mux=mkv,dst=\"%1\"}")
        .arg(formatSoutPath(path));
    libvlc_media_add_option(media, sout.toUtf8().constData());
    libvlc_media_add_option(media, ":sout-keep");
    return media;
}

bool VlcWidget::startPlaybackPlayer(Status pendingStatus)
{
    waitForPlayerCleanupFinished();
    resetStatsCache();

    libvlc_media_t *media = openPlaybackMedia(m_url);
    if (!media) {
        log(QString("[%1] Failed to create media for %2").arg(m_name, m_url), 3);
        return false;
    }

    m_player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (!m_player) {
        log(QString("[%1] Failed to create player").arg(m_name), 3);
        return false;
    }

    attachToSurface();
    setupEvents();
    if (libvlc_media_player_play(m_player) != 0) {
        log(QString("[%1] Failed to start playback").arg(m_name), 3);
        cleanupPlayer();
        return false;
    }

    setStatus(pendingStatus);
    return true;
}

void VlcWidget::play(const QString &url, const QString &name)
{
    stop();

    m_url = url;
    m_name = name;
    m_reconnecting = false;
    m_hasConnectedOnce = false;
    m_reconnectCount = 0;

    m_placeholder->hide();
    m_videoSurface->show();

    m_nameLabel->setText(name);
    m_nameLabel->show();

    if (!startPlaybackPlayer(Status::Connecting)) {
        setStatus(Status::Failed);
        showStatus("Player Error");
        updateRecordUi();
        return;
    }

    log(QString("[%1] Connecting to %2").arg(m_name, url), 0);

    updateRecordUi();
}

void VlcWidget::setChannelInfo(const QString &name, const QString &url)
{
    m_name = name;
    m_url = url;
    m_nameLabel->setText(name);
    updateRecordUi();
}

void VlcWidget::setStreamInfo(const QString &sourceUrl, bool relayEnabled, const QString &relayPath)
{
    m_sourceUrl = sourceUrl.trimmed();
    m_relayEnabled = relayEnabled;
    m_relayPath = relayPath.trimmed();
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

void VlcWidget::setupRecordingEvents()
{
    if (!m_recordPlayer) return;

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_recordPlayer);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, onRecordEndReached, this);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, onRecordError, this);
}

void VlcWidget::detachRecordingEvents()
{
    if (!m_recordPlayer) return;

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_recordPlayer);
    libvlc_event_detach(em, libvlc_MediaPlayerEndReached, onRecordEndReached, this);
    libvlc_event_detach(em, libvlc_MediaPlayerEncounteredError, onRecordError, this);
}

bool VlcWidget::isCurrentRecordingEventSource(const void *eventSource) const
{
    if (!m_recordPlayer || !eventSource) return false;
    if (eventSource == m_recordPlayer) return true;

    libvlc_event_manager_t *em = libvlc_media_player_event_manager(m_recordPlayer);
    return eventSource == em;
}

QFuture<void> VlcWidget::cleanupPlayer()
{
    if (!m_player) {
        return m_playerCleanup.isRunning() ? m_playerCleanup : QtFuture::makeReadyVoidFuture();
    }

    detachEvents();

    auto *player = m_player;
    m_player = nullptr;
    // 비동기로 정리 (UI 블로킹 방지). future 는 호출자가 필요하면 대기.
    m_playerCleanup = QtConcurrent::run([player]() {
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);
    });
    return m_playerCleanup;
}

QFuture<void> VlcWidget::cleanupRecordingPlayer()
{
    if (!m_recordPlayer) {
        return m_recordCleanup.isRunning() ? m_recordCleanup : QtFuture::makeReadyVoidFuture();
    }

    detachRecordingEvents();

    auto *player = m_recordPlayer;
    m_recordPlayer = nullptr;
    m_recordCleanup = QtConcurrent::run([player]() {
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);
    });
    return m_recordCleanup;
}

void VlcWidget::waitForCleanupFinished()
{
    waitForPlayerCleanupFinished();
    if (m_recordCleanup.isRunning()) {
        m_recordCleanup.waitForFinished();
    }
}

void VlcWidget::waitForPlayerCleanupFinished()
{
    if (m_playerCleanup.isRunning()) {
        m_playerCleanup.waitForFinished();
    }
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
        self->m_hasConnectedOnce = true;
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

        self->m_stats.nominal_fps = self->m_cachedFps;
        self->log(QString("[%1] Stream connected").arg(self->m_name), 1);
    }, Qt::QueuedConnection);
}

void VlcWidget::onEndReached(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        const bool initialFailure = !self->m_hasConnectedOnce && self->m_reconnectCount == 0;
        if (self->m_recordingUsesPlaybackPlayer
            && (self->m_recState == RecState::Starting || self->m_recState == RecState::Active)) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
        }

        if (!self->m_reconnecting) {
            self->m_reconnectCount = 0;
        }
        self->m_reconnecting = true;
        self->setStatus(Status::Disconnected);
        self->showStatus("끊김");
        self->log(initialFailure
            ? QString("[%1] Initial connection ended; retrying").arg(self->m_name)
            : QString("[%1] Disconnected").arg(self->m_name), 2);
        if (self->m_autoReconnect && !self->m_reconnectTimer->isActive()) {
            self->m_reconnectTimer->start(initialFailure ? 1000 : 5000);
        }
    }, Qt::QueuedConnection);
}

void VlcWidget::onError(const libvlc_event_t *, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    QMetaObject::invokeMethod(self, [self]() {
        const bool initialFailure = !self->m_hasConnectedOnce && self->m_reconnectCount == 0;
        if (self->m_recordingUsesPlaybackPlayer
            && (self->m_recState == RecState::Starting || self->m_recState == RecState::Active)) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
        }

        if (!self->m_reconnecting) {
            self->m_reconnectCount = 0;
        }
        self->m_reconnecting = true;
        self->setStatus(Status::Disconnected);
        self->showStatus("연결 오류");
        self->log(initialFailure
            ? QString("[%1] Initial connection failed; retrying").arg(self->m_name)
            : QString("[%1] Connection Error").arg(self->m_name),
            initialFailure ? 2 : 3);
        if (self->m_autoReconnect && !self->m_reconnectTimer->isActive()) {
            self->m_reconnectTimer->start(initialFailure ? 1000 : 5000);
        }
    }, Qt::QueuedConnection);
}

void VlcWidget::onRecordEndReached(const libvlc_event_t *event, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    const void *eventSource = event ? event->p_obj : nullptr;
    QMetaObject::invokeMethod(self, [self, eventSource]() {
        if (!self->isCurrentRecordingEventSource(eventSource)) return;

        if (self->m_recState == RecState::Starting && !self->m_recordingUsesPlaybackPlayer) {
            const QString path = self->m_recordingPath;
            self->failRecordingStartup(path, "recording stream ended during startup");
            return;
        }

        if (self->m_recState == RecState::Active) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
        }
    }, Qt::QueuedConnection);
}

void VlcWidget::onRecordError(const libvlc_event_t *event, void *data)
{
    auto *self = static_cast<VlcWidget *>(data);
    const void *eventSource = event ? event->p_obj : nullptr;
    QMetaObject::invokeMethod(self, [self, eventSource]() {
        if (!self->isCurrentRecordingEventSource(eventSource)) return;

        if (self->m_recState == RecState::Starting && !self->m_recordingUsesPlaybackPlayer) {
            const QString path = self->m_recordingPath;
            self->failRecordingStartup(path, "recording stream failed during startup");
            return;
        }

        if (self->m_recState == RecState::Active) {
            self->stopRecordingInternal(RecStopReason::Disconnect);
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
    resetStatsCache();

    setStatus(Status::Reconnecting);
    showStatus(QString("재연결중... (%1/%2)").arg(m_reconnectCount).arg(MAX_RECONNECT));
    log(QString("[%1] Reconnecting... attempt %2").arg(m_name).arg(m_reconnectCount), 2);

    if (!startPlaybackPlayer(Status::Reconnecting)) {
        m_reconnectTimer->start();
    }
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

    m_url = url;
    m_name = name;
    resetStatsCache();
    showStatus("재연결중...");
    log(QString("[%1] Reconnecting...").arg(m_name), 2);

    if (!startPlaybackPlayer(Status::Reconnecting)) {
        fallbackToReconnect();
    }
}

void VlcWidget::updateTime(const QString &timeStr)
{
    Q_UNUSED(timeStr);
    updateRecordUi();
}

void VlcWidget::stop()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    m_reconnecting = false;

    // 녹화 중이었다면 Remove 의미로 마무리 (녹화 player flush 만 비동기)
    if (m_recState != RecState::Idle) {
        finalizeRecordingForShutdown();
    }

    cleanupPlayer();  // m_player 가 이미 null 이면 no-op

    m_url.clear();
    m_name.clear();
    m_cachedFps = 0.0;
    resetStatsCache();
    m_videoSurface->hide();
    m_nameLabel->hide();
    m_placeholder->show();

    updateRecordUi();
}

bool VlcWidget::isPlaying() const
{
    if (!m_player) return false;
    return libvlc_media_player_is_playing(m_player);
}

void VlcWidget::resetStatsCache()
{
    m_stats = Stats{};
    m_stats.nominal_fps = m_cachedFps;
    m_hasStatsSample = false;
    m_lastStatsAtMs = 0;
    m_lastDisplayedPictures = 0;
    m_lastLostPictures = 0;
    m_lastDemuxCorrupted = 0;
    m_lastDemuxDiscontinuity = 0;
}

void VlcWidget::refreshStats(qint64 nowMs)
{
    Stats s{};
    s.updated_at_ms = nowMs;
    s.playing = isPlaying();
    s.nominal_fps = m_cachedFps;

    if (!m_player || !s.playing) {
        m_stats = s;
        m_hasStatsSample = false;
        m_lateStreakSamples = 0;
        return;
    }

    libvlc_media_t *media = libvlc_media_player_get_media(m_player);
    if (media) {
        libvlc_media_stats_t stats{};
        if (libvlc_media_get_stats(media, &stats)) {
            // libVLC bitrate 단위: bytes/µs → × 8000 = kbit/s
            s.valid = true;
            s.input_bitrate_kbps = static_cast<double>(stats.f_input_bitrate) * 8000.0;
            s.demux_bitrate_kbps = static_cast<double>(stats.f_demux_bitrate) * 8000.0;
            s.send_bitrate_kbps = static_cast<double>(stats.f_send_bitrate) * 8000.0;
            s.bitrate_kbps = s.demux_bitrate_kbps;

            s.read_bytes = stats.i_read_bytes;
            s.demux_read_bytes = stats.i_demux_read_bytes;
            s.sent_bytes = stats.i_sent_bytes;

            s.demux_corrupted = stats.i_demux_corrupted;
            s.demux_discontinuity = stats.i_demux_discontinuity;
            s.decoded_video = stats.i_decoded_video;
            s.decoded_audio = stats.i_decoded_audio;
            s.displayed_pictures = stats.i_displayed_pictures;
            s.lost_pictures = stats.i_lost_pictures;
            s.played_abuffers = stats.i_played_abuffers;
            s.lost_abuffers = stats.i_lost_abuffers;
            s.sent_packets = stats.i_sent_packets;

            if (m_hasStatsSample && nowMs > m_lastStatsAtMs) {
                const double elapsedSeconds = static_cast<double>(nowMs - m_lastStatsAtMs) / 1000.0;
                s.displayed_delta = qMax(0, s.displayed_pictures - m_lastDisplayedPictures);
                s.lost_delta = qMax(0, s.lost_pictures - m_lastLostPictures);
                s.corrupted_delta = qMax(0, s.demux_corrupted - m_lastDemuxCorrupted);
                s.discontinuity_delta = qMax(0, s.demux_discontinuity - m_lastDemuxDiscontinuity);
                if (elapsedSeconds > 0.0) {
                    s.output_fps = static_cast<double>(s.displayed_delta) / elapsedSeconds;
                    s.fps = s.output_fps;
                }
            }

            m_lastDisplayedPictures = s.displayed_pictures;
            m_lastLostPictures = s.lost_pictures;
            m_lastDemuxCorrupted = s.demux_corrupted;
            m_lastDemuxDiscontinuity = s.demux_discontinuity;
            m_lastStatsAtMs = nowMs;
            m_hasStatsSample = true;
        }
        libvlc_media_release(media);
    }

    m_stats = s;
    maybeFlushForLatency(nowMs, s);
}

void VlcWidget::maybeFlushForLatency(qint64 nowMs, const Stats &s)
{
    // 연결 상태에서만 동작 (Connecting/Reconnecting 중엔 streak 리셋)
    if (m_status != Status::Connected) {
        m_lateStreakSamples = 0;
        return;
    }

    // "늦은 프레임이 지속적으로 드롭" = 밀림 고착 추정 신호.
    // (UDP 에선 demux discontinuity 가 정상적으로도 잦으므로 lost_pictures 만 신호로 사용)
    const bool badSecond = s.valid && s.lost_delta > 0;
    if (!badSecond) {
        m_lateStreakSamples = 0;
        return;
    }

    if (++m_lateStreakSamples < WATCHDOG_LATE_STREAK) return;
    if (m_lastWatchdogFlushMs != 0 && nowMs - m_lastWatchdogFlushMs < WATCHDOG_MIN_INTERVAL_MS) return;

    m_lastWatchdogFlushMs = nowMs;
    m_lateStreakSamples = 0;
    log(QString("[%1] 지연 누적 감지 → 스트림 flush(재접속)").arg(m_name), 2);

    // stats 루프(클럭 타이머) 밖에서 실행 — player 교체의 블로킹 정리를 루프 안에서 피함
    QPointer<VlcWidget> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (self) self->reconnect();
    });
}

VlcWidget::Stats VlcWidget::getStats() const
{
    return m_stats;
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
    auto *infoAction = menu.addAction("채널 정보");
    auto *editAction = menu.addAction("채널 수정");
    auto *snapshotAction = menu.addAction("스냅샷 저장");
    VlcWidget *t = recordingTarget();
    const bool rec = t && t->m_recState == RecState::Active;
    const bool busy = t && (t->m_recState == RecState::Starting || t->m_recState == RecState::Stopping);
    auto *recordAction = menu.addAction(rec ? "녹화 중지" : "녹화 시작");
    menu.addSeparator();
    auto *reconnectAction = menu.addAction("재연결");
    menu.addSeparator();
    auto *removeAction = menu.addAction("채널 삭제");

    fullscreenAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);
    infoAction->setEnabled(!m_url.isEmpty() && (!m_isFullscreen || m_sourceViewer));
    editAction->setEnabled(!m_url.isEmpty() && (!m_isFullscreen || m_sourceViewer));
    snapshotAction->setEnabled(isPlaying());
    recordAction->setEnabled(t && !busy && (rec || t->isPlaying()));
    reconnectAction->setEnabled(!m_url.isEmpty() && m_status != Status::Connected && m_status != Status::Connecting);
    removeAction->setEnabled(!m_url.isEmpty() && !m_isFullscreen);

    auto *selected = menu.exec(globalPos);
    if (!selected) return;

    if (selected == fullscreenAction) {
        emit requestFullscreen(this);
    } else if (selected == infoAction) {
        emit requestInfo(m_isFullscreen && m_sourceViewer ? m_sourceViewer.data() : this);
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

    // 2차 폴백: libVLC snapshot 이 실패하면 네이티브 비디오 서피스 영역을 OS-level 스크린 그랩.
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

// ============================================================
// 녹화 — 시작
// ============================================================

void VlcWidget::startRecording()
{
    VlcWidget *t = recordingTarget();
    if (t != this) { t->startRecording(); return; }

    if (m_recState != RecState::Idle || m_recordPlayer || m_recordCleanup.isRunning()
        || !isPlaying() || m_url.isEmpty()) {
        return;
    }

    const QString path = makeRecordingPath();
    m_recordingPath = path;
    m_recordingStartTime = QDateTime::currentDateTime();
    m_recordingUsesPlaybackPlayer = false;
    setRecState(RecState::Starting);

    libvlc_media_t *media = openRecordingMedia(m_url, path);
    if (!media) {
        m_recordingPath.clear();
        setRecState(RecState::Idle);
        emit recordingFailed(this, path, "media/player creation failed");
        log(QString("[%1] Recording failed (creation): %2").arg(m_name, path), 3);
        return;
    }

    m_recordPlayer = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (!m_recordPlayer) {
        m_recordingPath.clear();
        setRecState(RecState::Idle);
        emit recordingFailed(this, path, "media/player creation failed");
        log(QString("[%1] Recording failed (player): %2").arg(m_name, path), 3);
        return;
    }

    setupRecordingEvents();
    if (libvlc_media_player_play(m_recordPlayer) != 0) {
        failRecordingStartup(path, "recording stream failed to start");
        return;
    }

    startRecordingWatchdog(path);
}

void VlcWidget::startRecordingWatchdog(const QString &path)
{
    m_recWatchdogAttempts = 0;
    scheduleWatchdogTick(path);
}

void VlcWidget::failRecordingStartup(const QString &path, const QString &reason)
{
    if (m_recState != RecState::Starting) return;

    QFuture<void> flushFuture = cleanupRecordingPlayer();
    m_recordingPath.clear();
    m_recordingUsesPlaybackPlayer = false;
    setRecState(RecState::Stopping);

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
        [this, path, reason, watcher]() {
            setRecState(RecState::Idle);
            log(QString("[%1] Recording failed: %2 (%3)").arg(m_name, path, reason), 3);
            emit recordingFailed(this, path, reason);
            watcher->deleteLater();
        });
    watcher->setFuture(flushFuture);
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
            failRecordingStartup(path, reason);
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
    if (m_recState == RecState::Stopping) {
        if (reason == RecStopReason::Remove && m_recordCleanup.isRunning()) {
            m_recordCleanup.waitForFinished();
            setRecState(RecState::Idle);
        }
        return;
    }

    const QString path = m_recordingPath;
    const QDateTime start = m_recordingStartTime;
    const QDateTime stoppedAt = QDateTime::currentDateTime();
    const bool wasActive = (m_recState == RecState::Active);
    const bool byDisc = (reason == RecStopReason::Disconnect);
    const bool isRemove = (reason == RecStopReason::Remove);
    const bool usedPlaybackPlayer = m_recordingUsesPlaybackPlayer;

    m_recordingPath.clear();
    m_recordingUsesPlaybackPlayer = false;
    setRecState(RecState::Stopping);

    // 현재 녹화중 플레이어 cleanup → 파일 finalizer 완료를 future 로 추적
    QFuture<void> flushFuture = usedPlaybackPlayer
        ? cleanupPlayer()
        : cleanupRecordingPlayer();

    if (isRemove) {
        flushFuture.waitForFinished();
        setRecState(RecState::Idle);
        return;
    }

    if (usedPlaybackPlayer && reason == RecStopReason::Manual) {
        flushFuture.waitForFinished();
        if (!startPlaybackPlayer(Status::Connecting)) {
            fallbackToReconnect();
        }
        setRecState(RecState::Idle);
        if (wasActive) {
            qint64 bytes = QFileInfo(path).size();
            int dur = start.secsTo(stoppedAt);
            if (dur < 0) dur = 0;
            emit recordingStopped(this, path, bytes, dur, byDisc);
        } else {
            emit recordingFailed(this, path,
                byDisc ? "disconnected during startup" : "stopped during startup");
        }
        return;
    }

    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this,
        [this, path, start, stoppedAt, wasActive, byDisc, watcher]() {
            setRecState(RecState::Idle);
            if (wasActive) {
                qint64 bytes = QFileInfo(path).size();
                int dur = start.secsTo(stoppedAt);
                if (dur < 0) dur = 0;
                emit recordingStopped(this, path, bytes, dur, byDisc);
            } else {
                // Starting 이었다가 Disconnect/Manual 로 내려간 경우 → Failed
                emit recordingFailed(this, path,
                    byDisc ? "disconnected during startup" : "stopped during startup");
            }
            watcher->deleteLater();
        });
    watcher->setFuture(flushFuture);
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
            m_recordBtn->setEnabled(t && t->isPlaying() && !t->m_recordCleanup.isRunning());
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
        case RecState::Stopping:
            if (stateChanged) {
                m_recBadge->setStyleSheet(Style::REC_BADGE_STARTING);
                m_recBadge->setText("● 저장 중…");
                m_recBadge->show();
                m_recordBtn->setText("■");
                m_recordBtn->setToolTip("녹화 저장 중…");
                m_recordBtn->setStyleSheet(Style::TOOL_BUTTON);
                m_recordBtn->setEnabled(false);
            }
            break;
    }

    m_lastDisplayedRec = sAsInt;
    m_snapshotBtn->setEnabled(isPlaying());
}
