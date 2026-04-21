#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
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
    enum class RecState { Idle, Starting, Active };
    enum class RecStopReason { Manual, Disconnect, Remove };

    explicit VlcWidget(libvlc_instance_t *vlcInstance, QWidget *parent = nullptr);
    ~VlcWidget();

    struct Stats {
        double fps;
        double bitrate_kbps;
        int displayed_pictures;
        int lost_pictures;
        bool playing;
    };

    void play(const QString &url, const QString &name = QString());
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
    void updateTime(const QString &timeStr);
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

public slots:
    void updateRecordUi();

signals:
    void doubleClicked(VlcWidget *widget);
    void requestFullscreen(VlcWidget *widget);
    void requestRemove(VlcWidget *widget);
    void snapshotTaken(const QString &filePath);
    void statusChanged(VlcWidget *widget, VlcWidget::Status status);

    // 녹화 시그널
    void recordingStateChanged(VlcWidget *w, VlcWidget::RecState newState);       // 즉시 UI 용
    void recordingStarted(VlcWidget *w, const QString &path);                      // 로깅: Active 확정
    void recordingStopped(VlcWidget *w, const QString &path,
                          qint64 bytes, int durationSec, bool byDisconnect);       // 로깅: flush 완료
    void recordingFailed(VlcWidget *w, const QString &path, const QString &reason);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_player = nullptr;
    QWidget *m_videoSurface = nullptr;
    QLabel *m_placeholder = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLabel *m_infoLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_recBadge = nullptr;
    QPushButton *m_snapshotBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QString m_url;
    QString m_name;
    bool m_autoReconnect = true;
    bool m_isFullscreen = false;
    bool m_reconnecting = false;
    Status m_status = Status::Idle;
    void setStatus(Status s);
    static const int MAX_RECONNECT = 30;

    // 녹화 상태
    RecState m_recState = RecState::Idle;
    QString m_recordingPath;
    QDateTime m_recordingStartTime;
    int m_recWatchdogAttempts = 0;

    // UI 성능: stylesheet 반복 재적용 방지용 캐시 (-1 = 아직 미적용)
    int m_lastDisplayedRec = -1;

    // 비디오 트랙 fps 캐시 — 연결 세션 동안 고정값이므로 반복 조회 회피
    double m_cachedFps = 0.0;

    std::function<void(const QString &, int)> m_logCallback;
    void log(const QString &msg, int level = 1);
    void attachToSurface();
    void detachEvents();
    void setupEvents();
    void showStatus(const QString &text);
    void showContextMenu(const QPoint &globalPos);
    void tryReconnect();
    QFuture<void> cleanupPlayer();

    // 녹화 헬퍼
    VlcWidget *recordingTarget();
    void setRecState(RecState s);
    libvlc_media_t *openMedia(const QString &url, bool withRecording);
    bool restartPlayerPreserving(bool withRecording);
    bool openAndPlayNoRecording();
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

    static void onPlaying(const libvlc_event_t *event, void *data);
    static void onEndReached(const libvlc_event_t *event, void *data);
    static void onError(const libvlc_event_t *event, void *data);
};
