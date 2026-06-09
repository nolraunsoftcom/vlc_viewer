#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

class MediaRelayManager : public QObject
{
    Q_OBJECT

public:
    struct Channel {
        QString name;
        QString sourceUrl;
        QString relayPath;
    };

    explicit MediaRelayManager(QObject *parent = nullptr);
    ~MediaRelayManager() override;

    void setChannels(const QVector<Channel> &channels);
    bool ensureRunning();
    void stop();

    QString playUrlForPath(const QString &relayPath) const;
    QString configPath() const;
    QString executablePath() const;
    bool hasChannels() const { return !m_channels.isEmpty(); }

    static bool isValidRelayPath(const QString &relayPath);
    static QString normalizeRelayPath(const QString &relayPath);

signals:
    void logMessage(const QString &message, int level);

private:
    QProcess *m_process = nullptr;
    QVector<Channel> m_channels;
    QString m_lastConfigSignature;
    bool m_intentionalStop = false;   // stopProcess() 로 의도적으로 끈 경우 = true (크래시 오탐 방지)

    static const quint16 RTSP_PORT = 8554;   // rtspAddress :8554
    static const quint16 API_PORT  = 9997;   // apiAddress 127.0.0.1:9997

    QString findExecutable() const;
    QString buildConfig(QString *signatureOut) const;
    bool writeConfig(const QString &config);
    bool startProcess();
    void stopProcess();

    // robustness 헬퍼
    bool isPortInUse(quint16 port) const;     // 127.0.0.1:<port> 점유 여부 탐지
    void killStaleProcesses();                // 이전 실행에서 남은(고아) mediamtx 정리
    bool waitForPortsFree(int timeoutMs);     // 포트가 비워질 때까지 대기
};
