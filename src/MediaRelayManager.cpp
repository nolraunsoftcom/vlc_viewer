#include "MediaRelayManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString yamlQuoted(QString value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    return QStringLiteral("\"%1\"").arg(value);
}

} // namespace

MediaRelayManager::MediaRelayManager(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                emit logMessage(QStringLiteral("MediaMTX: %1").arg(line.trimmed()), 0);
            }
        }
    });

    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_channels.isEmpty()) return;
                const int level = exitStatus == QProcess::NormalExit ? 2 : 3;
                emit logMessage(QStringLiteral("MediaMTX stopped (exit=%1)").arg(exitCode), level);
            });
}

MediaRelayManager::~MediaRelayManager()
{
    stop();
}

void MediaRelayManager::setChannels(const QVector<Channel> &channels)
{
    QVector<Channel> normalized;
    normalized.reserve(channels.size());

    QSet<QString> seenPaths;
    for (const auto &channel : channels) {
        Channel next = channel;
        next.sourceUrl = next.sourceUrl.trimmed();
        next.relayPath = normalizeRelayPath(next.relayPath);

        if (next.sourceUrl.isEmpty() || !isValidRelayPath(next.relayPath)) {
            continue;
        }

        if (seenPaths.contains(next.relayPath)) {
            continue;
        }
        seenPaths.insert(next.relayPath);
        normalized.append(next);
    }

    m_channels = normalized;
}

bool MediaRelayManager::ensureRunning()
{
    if (m_channels.isEmpty()) {
        stop();
        return true;
    }

    QString signature;
    const QString config = buildConfig(&signature);
    const bool configChanged = signature != m_lastConfigSignature;
    const bool alreadyRunning = m_process->state() != QProcess::NotRunning;

    if (alreadyRunning && !configChanged) {
        return true;
    }

    if (!writeConfig(config)) {
        emit logMessage(QStringLiteral("MediaMTX config write failed: %1").arg(configPath()), 3);
        return false;
    }

    if (alreadyRunning) {
        stopProcess();
    }

    if (!startProcess()) {
        return false;
    }

    m_lastConfigSignature = signature;
    return true;
}

void MediaRelayManager::stop()
{
    stopProcess();
    m_lastConfigSignature.clear();
}

QString MediaRelayManager::playUrlForPath(const QString &relayPath) const
{
    const QString path = normalizeRelayPath(relayPath);
    return QStringLiteral("rtsp://127.0.0.1:8554/%1").arg(path);
}

QString MediaRelayManager::configPath() const
{
    const QString dir = QDir::homePath() + QStringLiteral("/.ziilab");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/mediamtx.yml");
}

QString MediaRelayManager::executablePath() const
{
    return findExecutable();
}

bool MediaRelayManager::isValidRelayPath(const QString &relayPath)
{
    const QString path = normalizeRelayPath(relayPath);
    if (path.isEmpty()) return false;

    for (const QChar ch : path) {
        if (ch.isLetterOrNumber()
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('.')) {
            continue;
        }
        return false;
    }
    return true;
}

QString MediaRelayManager::normalizeRelayPath(const QString &relayPath)
{
    QString path = relayPath.trimmed();
    path.replace('\\', '/');
    while (path.startsWith('/')) {
        path.remove(0, 1);
    }
    while (path.endsWith('/')) {
        path.chop(1);
    }
    return path;
}

QString MediaRelayManager::findExecutable() const
{
    const QString configured = qEnvironmentVariable("ZIILAB_MEDIAMTX");
    if (!configured.isEmpty()) {
        QFileInfo fi(configured);
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("mediamtx"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    QStringList candidates;
#if defined(Q_OS_WIN)
    candidates << appDir.filePath(QStringLiteral("mediamtx.exe"));
#else
    candidates << appDir.filePath(QStringLiteral("mediamtx"));
#endif
#if defined(Q_OS_MAC)
    candidates << appDir.filePath(QStringLiteral("../Resources/mediamtx"));
    candidates << appDir.filePath(QStringLiteral("../Frameworks/mediamtx"));
#endif

    for (const QString &candidate : candidates) {
        QFileInfo fi(candidate);
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }

    return QString();
}

QString MediaRelayManager::buildConfig(QString *signatureOut) const
{
    QString config;
    QTextStream out(&config);

    out << "logLevel: info\n";
    out << "logDestinations: [stdout]\n\n";
    out << "api: true\n";
    out << "apiAddress: 127.0.0.1:9997\n";
    out << "metrics: false\n";
    out << "pprof: false\n";
    out << "playback: false\n\n";
    out << "rtsp: true\n";
    out << "rtspTransports: [tcp]\n";
    out << "rtspAddress: :8554\n";
    out << "\n";
    out << "rtmp: false\n";
    out << "hls: false\n";
    out << "webrtc: false\n";
    out << "srt: false\n";
    out << "moq: false\n\n";
    out << "paths:\n";

    QString signature;
    for (const auto &channel : m_channels) {
        out << "  " << channel.relayPath << ":\n";
        out << "    source: " << yamlQuoted(channel.sourceUrl) << "\n";
        out << "    sourceOnDemand: false\n";
        out << "    rtspTransport: udp\n\n";
        out << "    record: false\n";
        out << "    recordPath: ./recordings/%path/%Y-%m-%d_%H-%M-%S-%f\n";
        out << "    recordFormat: fmp4\n";
        out << "    recordPartDuration: 1s\n";
        out << "    recordSegmentDuration: 15m\n";
        out << "    recordDeleteAfter: 0s\n";

        signature += channel.relayPath + QLatin1Char('|') + channel.sourceUrl + QLatin1Char('\n');
    }

    if (signatureOut) {
        *signatureOut = signature;
    }
    return config;
}

bool MediaRelayManager::writeConfig(const QString &config)
{
    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray bytes = config.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool MediaRelayManager::startProcess()
{
    const QString executable = findExecutable();
    if (executable.isEmpty()) {
        emit logMessage(QStringLiteral("MediaMTX executable not found. Install mediamtx or set ZIILAB_MEDIAMTX."), 3);
        return false;
    }

    m_process->setProgram(executable);
    m_process->setArguments({configPath()});
    m_process->start();

    if (!m_process->waitForStarted(1500)) {
        emit logMessage(QStringLiteral("MediaMTX failed to start: %1").arg(m_process->errorString()), 3);
        return false;
    }

    emit logMessage(QStringLiteral("MediaMTX started: %1").arg(configPath()), 1);
    return true;
}

void MediaRelayManager::stopProcess()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_process->terminate();
    if (!m_process->waitForFinished(2500)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
