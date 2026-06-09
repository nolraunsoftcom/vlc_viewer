#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QDateTime>
#include <QFuture>
#include <vlc/vlc.h>
#include <functional>

class VlcWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Status { Idle, Connecting, Connected, Disconnected, Reconnecting, Failed };
    enum class RecState { Idle, Starting, Active, Stopping };
    enum class RecStopReason { Manual, Disconnect, Remove };

    explicit VlcWidget(libvlc_instance_t *vlcInstance, QWidget *parent = nullptr);
    ~VlcWidget();

    struct Stats {
        bool playing = false;
        bool valid = false;

        double fps = 0.0;               // output_fps alias for existing summaries
        double nominal_fps = 0.0;       // video track metadata
        double output_fps = 0.0;        // displayed_pictures delta / elapsed

        double bitrate_kbps = 0.0;      // demux_bitrate_kbps alias
        double input_bitrate_kbps = 0.0;
        double demux_bitrate_kbps = 0.0;
        double send_bitrate_kbps = 0.0;

        qint64 read_bytes = 0;
        qint64 demux_read_bytes = 0;
        qint64 sent_bytes = 0;

        int demux_corrupted = 0;
        int demux_discontinuity = 0;
        int decoded_video = 0;
        int decoded_audio = 0;
        int displayed_pictures = 0;
        int lost_pictures = 0;
        int played_abuffers = 0;
        int lost_abuffers = 0;
        int sent_packets = 0;

        int displayed_delta = 0;
        int lost_delta = 0;
        int corrupted_delta = 0;
        int discontinuity_delta = 0;

        qint64 updated_at_ms = 0;
    };

    void play(const QString &url, const QString &name = QString());
    void setChannelInfo(const QString &name, const QString &url);
    void reconnect();
    void setLogCallback(std::function<void(const QString &, int)> callback) { m_logCallback = callback; }
    void setAutoReconnect(bool enabled) { m_autoReconnect = enabled; }
    bool autoReconnect() const { return m_autoReconnect; }
    void setFullscreenMode(bool fs) { m_isFullscreen = fs; }
    bool isFullscreenMode() const { return m_isFullscreen; }
    void stop();
    bool isPlaying() const;
    QString url() const { return m_url; }
    QString name() const { return m_name; }
    QString sourceUrl() const { return m_sourceUrl.isEmpty() ? m_url : m_sourceUrl; }
    bool relayEnabled() const { return m_relayEnabled; }
    QString relayPath() const { return m_relayPath; }
    void setStreamInfo(const QString &sourceUrl, bool relayEnabled, const QString &relayPath);
    void updateTime(const QString &timeStr);
    void refreshStats(qint64 nowMs);
    Stats getStats() const;
    bool takeSnapshot(const QString &filePath);

    Status status() const { return m_status; }
    static QString statusText(Status s);

    // 녹화 API
    void startRecording();
    void stopRecording();
    void finalizeRecordingForShutdown();   // 소멸/종료 경로 전용 (재생 복구 X)
    RecState recState() const { return m_recState; }
    QString recordingPath() const { return m_recordingPath; }
    QDateTime recordingStartTime() const { return m_recordingStartTime; }
    void setSourceViewer(VlcWidget *viewer) { m_sourceViewer = viewer; }
    VlcWidget *sourceViewer() const { return m_sourceViewer.data(); }

public slots:
    void updateRecordUi();

signals:
    void doubleClicked(VlcWidget *widget);
    void requestFullscreen(VlcWidget *widget);
    void requestInfo(VlcWidget *widget);
    void requestEdit(VlcWidget *widget);
    void requestRemove(VlcWidget *widget);
    void snapshotTaken(const QString &filePath);
    void statusChanged(VlcWidget *widget, VlcWidget::Status status);

    // 녹화 시그널
    void recordingStateChanged(VlcWidget *w, VlcWidget::RecState newState);       // 즉시 UI 용
    void recordingStarted(VlcWidget *w, const QString &path);                      // 로깅: Active 확정
    void recordingStopped(VlcWidget *w, const QString &path,
                          qint64 bytes, int durationSec, bool byDisconnect);       // 로깅: flush 완료, durationSec 는 중지 시점 기준
    void recordingFailed(VlcWidget *w, const QString &path, const QString &reason);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_player = nullptr;
    libvlc_media_player_t *m_recordPlayer = nullptr;
    QWidget *m_videoSurface = nullptr;
    QLabel *m_placeholder = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_recBadge = nullptr;
    QPushButton *m_snapshotBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QString m_url;
    QString m_name;
    QString m_sourceUrl;
    QString m_relayPath;
    bool m_relayEnabled = false;
    bool m_autoReconnect = true;
    bool m_isFullscreen = false;
    bool m_reconnecting = false;
    bool m_hasConnectedOnce = false;
    Status m_status = Status::Idle;
    void setStatus(Status s);
    // 재접속 정책 (relay TIME_WAIT 폭증 방지): 빠른 포기 + exponential backoff + 자동 중단.
    // 카메라 OFF 시 5초 간격 무한 재시도로 로컬 RTSP 연결이 누적되던 문제 대응.
    static constexpr int INITIAL_MAX_RECONNECT = 3;        // 한 번도 연결된 적 없는 채널(카메라 OFF 등)
    static constexpr int ESTABLISHED_MAX_RECONNECT = 10;   // 연결된 적 있는 채널(현장 흔들림 복구 유지)
    static constexpr int RECONNECT_BASE_MS = 5000;         // backoff 기준: 5 → 10 → 20 → 40 → 60(s)
    static constexpr int RECONNECT_MAX_INTERVAL_MS = 60000;

    // 녹화 상태
    RecState m_recState = RecState::Idle;
    bool m_recordingUsesPlaybackPlayer = false;
    QString m_recordingPath;
    QDateTime m_recordingStartTime;
    int m_recWatchdogAttempts = 0;
    QPointer<VlcWidget> m_sourceViewer;

    // UI 성능: stylesheet 반복 재적용 방지용 캐시 (-1 = 아직 미적용)
    int m_lastDisplayedRec = -1;

    // 비디오 트랙 fps 캐시 — 연결 세션 동안 고정값이므로 반복 조회 회피
    double m_cachedFps = 0.0;
    Stats m_stats;
    bool m_hasStatsSample = false;
    qint64 m_lastStatsAtMs = 0;
    int m_lastDisplayedPictures = 0;
    int m_lastLostPictures = 0;
    int m_lastDemuxCorrupted = 0;
    int m_lastDemuxDiscontinuity = 0;

    // 지연 누적 워치독 — 무선 음영지역에서 밀림이 고착될 때 stream 을 flush(재접속)해 회복.
    // 옵션 정상화(clock 재동기화 복원) 후에도 RF 하드 드롭으로 고착되는 드문 경우용 2차 안전망.
    int m_lateStreakSamples = 0;          // 늦은 프레임 드롭이 연속된 초(스탯 1Hz)
    qint64 m_lastWatchdogFlushMs = 0;     // 마지막 워치독 flush 시각 (레이트리밋)
    static const int WATCHDOG_LATE_STREAK = 10;        // 10초 연속 드롭 지속 시 flush
    static const int WATCHDOG_MIN_INTERVAL_MS = 60000; // 최소 60초 간격
    void maybeFlushForLatency(qint64 nowMs, const Stats &s);

    std::function<void(const QString &, int)> m_logCallback;
    void log(const QString &msg, int level = 1);
    void resetStatsCache();
    void attachToSurface();
    void detachEvents();
    void setupEvents();
    void detachRecordingEvents();
    void setupRecordingEvents();
    bool isCurrentRecordingEventSource(const void *eventSource) const;
    void showStatus(const QString &text);
    void showContextMenu(const QPoint &globalPos);
    void tryReconnect();
    int reconnectIntervalMs() const;   // 현재 상태 기준 다음 재시도 간격(backoff)
    void scheduleReconnect();          // giveUp/autoReconnect 확인 후 backoff 간격으로 타이머 무장
    QFuture<void> cleanupPlayer();
    QFuture<void> cleanupRecordingPlayer();
    void waitForCleanupFinished();
    void waitForPlayerCleanupFinished();

    // 녹화 헬퍼
    VlcWidget *recordingTarget();
    void setRecState(RecState s);
    libvlc_media_t *openPlaybackMedia(const QString &url);
    libvlc_media_t *openRecordingMedia(const QString &url, const QString &path);
    bool startPlaybackPlayer(Status pendingStatus);
    void failRecordingStartup(const QString &path, const QString &reason);
    void stopRecordingInternal(RecStopReason reason);
    void startRecordingWatchdog(const QString &path);
    void scheduleWatchdogTick(const QString &path);
    void fallbackToReconnect();
    void saveSnapshot();   // 버튼·메뉴 공용
    QString safeFileStem(const QString &name) const;
    QString formatSoutPath(const QString &absolutePath) const;
    QString makeRecordingPath() const;

    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectCount = 0;
    bool m_giveUp = false;   // 자동 재연결 포기(Failed) 상태 — 수동 reconnect/재생 전까지 재무장 금지
    QFuture<void> m_playerCleanup;
    QFuture<void> m_recordCleanup;

    static void onPlaying(const libvlc_event_t *event, void *data);
    static void onEndReached(const libvlc_event_t *event, void *data);
    static void onError(const libvlc_event_t *event, void *data);
    static void onRecordEndReached(const libvlc_event_t *event, void *data);
    static void onRecordError(const libvlc_event_t *event, void *data);
};
