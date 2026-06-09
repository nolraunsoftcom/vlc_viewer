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
    bool waitForPortsFree(int timeoutMs);     // 포트가 비워질 때까지 대기

    // 고아 프로세스 회수: 이름 기반 일괄 kill 대신, "우리가 띄운 PID" 만 검증 후 종료한다.
    QString pidFilePath() const;              // 우리가 띄운 mediamtx PID 기록 파일
    void writePidFile(qint64 pid);
    void removePidFile();
    qint64 readPidFile() const;
    bool isMediamtxProcess(qint64 pid) const; // PID 재사용 방지: 실제 mediamtx 인지 확인
    bool killProcessByPid(qint64 pid);        // 특정 PID 만 종료
    bool reclaimStaleProcess();               // PID 파일 기준 우리 고아 mediamtx 만 회수
};
