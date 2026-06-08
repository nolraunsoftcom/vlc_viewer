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

    QString findExecutable() const;
    QString buildConfig(QString *signatureOut) const;
    bool writeConfig(const QString &config);
    bool startProcess();
    void stopProcess();
};
