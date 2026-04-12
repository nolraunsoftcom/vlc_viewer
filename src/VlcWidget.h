#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <vlc/vlc.h>
#include <functional>

class VlcWidget : public QWidget
{
    Q_OBJECT

public:
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
    void setLogCallback(std::function<void(const QString &, int)> callback) { m_logCallback = callback; }
    void stop();
    bool isPlaying() const;
    QString url() const { return m_url; }
    QString name() const { return m_name; }
    void updateTime(const QString &timeStr);
    Stats getStats() const;
    bool takeSnapshot(const QString &filePath);

signals:
    void doubleClicked(VlcWidget *widget);
    void requestFullscreen(VlcWidget *widget);
    void requestRemove(VlcWidget *widget);
    void snapshotTaken(const QString &filePath);

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
    QString m_url;
    QString m_name;

    std::function<void(const QString &, int)> m_logCallback;
    void log(const QString &msg, int level = 1);
    void attachToSurface();
    void setupEvents();
    void showStatus(const QString &text);
    void showContextMenu(const QPoint &globalPos);
    void tryReconnect();

    QTimer *m_reconnectTimer = nullptr;
    int m_reconnectCount = 0;

    static void onPlaying(const libvlc_event_t *event, void *data);
    static void onEndReached(const libvlc_event_t *event, void *data);
    static void onError(const libvlc_event_t *event, void *data);
};
