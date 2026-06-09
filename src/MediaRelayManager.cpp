#include "MediaRelayManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTextStream>
#include <QThread>

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

#if defined(Q_OS_WIN)
    // Windows: 자식 mediamtx 콘솔 창이 따로 뜨지 않도록 숨긴다(CREATE_NO_WINDOW = 0x08000000).
    // 사용자가 수동 실행한 것으로 오인 → 중복 실행/포트 충돌하던 혼선을 차단.
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) {
            args->flags |= 0x08000000;
        });
#endif

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
                if (m_intentionalStop) {
                    // 우리가 stop() 으로 끈 정상 종료.
                    emit logMessage(
                        QStringLiteral("MediaMTX relay stopped (intentional, exit=%1).").arg(exitCode), 1);
                    m_intentionalStop = false;
                    return;
                }
                // 의도하지 않은 종료 = 크래시/강제종료 가능성. 명확히 ERROR 로 남긴다.
                const QString how = (exitStatus == QProcess::CrashExit)
                    ? QStringLiteral("crash") : QStringLiteral("unexpected exit");
                emit logMessage(
                    QStringLiteral("MediaMTX relay 비정상 종료 (%1, code=%2). "
                                   "채널 변경/재실행 시 자동으로 다시 기동을 시도합니다.")
                        .arg(how).arg(exitCode),
                    3);
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
        if (m_process->state() != QProcess::NotRunning) {
            emit logMessage(QStringLiteral("relay 채널 없음 → MediaMTX 중지."), 1);
        }
        stop();
        return true;
    }

    QString signature;
    const QString config = buildConfig(&signature);
    const bool configChanged = signature != m_lastConfigSignature;
    const bool alreadyRunning = m_process->state() != QProcess::NotRunning;

    if (alreadyRunning && !configChanged) {
        emit logMessage(
            QStringLiteral("MediaMTX 이미 실행 중, 설정 변경 없음 (channels=%1).").arg(m_channels.size()), 0);
        return true;
    }

    if (!writeConfig(config)) {
        emit logMessage(QStringLiteral("MediaMTX config write 실패: %1").arg(configPath()), 3);
        return false;
    }
    emit logMessage(
        QStringLiteral("MediaMTX config 작성: %1 (channels=%2).").arg(configPath()).arg(m_channels.size()), 1);

    if (alreadyRunning) {
        emit logMessage(QStringLiteral("relay 설정 변경 감지 → MediaMTX 재시작."), 1);
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

    // MediaMTX path 이름 규칙(ASCII 영숫자/_/-/.)과 동일하게 검증한다.
    // 주의: QChar::isLetterOrNumber()는 한글 등 비-ASCII 문자도 letter로 인정한다.
    // 그대로 쓰면 한글 path가 통과 → MediaMTX가 "invalid path name"으로 기동 자체에
    // 실패해 relay 전체(=모든 채널)가 죽는다. 반드시 ASCII로 한정한다.
    for (const QChar ch : path) {
        const char latin = ch.toLatin1();
        if ((latin >= '0' && latin <= '9')
            || (latin >= 'a' && latin <= 'z')
            || (latin >= 'A' && latin <= 'Z')
            || latin == '_'
            || latin == '-'
            || latin == '.') {
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
    out << "rtspAddress: 127.0.0.1:8554\n";   // viewer 가 localhost 로만 접속 → 외부 노출/방화벽 팝업 차단
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
        out << "    rtspTransport: tcp\n\n";
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
        emit logMessage(
            QStringLiteral("MediaMTX 실행 파일을 찾지 못했습니다. mediamtx 를 viewer 옆에 두거나 "
                           "ZIILAB_MEDIAMTX 환경변수를 설정하세요."),
            3);
        return false;
    }
    emit logMessage(QStringLiteral("MediaMTX 실행 파일: %1").arg(executable), 0);

    // 이전 실행에서 남은 "우리" mediamtx 가 포트를 잡고 있으면 새 인스턴스가 bind 실패한다.
    // 단, 임의의 mediamtx 를 이름으로 일괄 종료하지 않는다(다른 인스턴스/사용자 프로세스 보호).
    // 우리가 기록한 PID 가 여전히 mediamtx 일 때만 그 PID 를 회수한다.
    if (isPortInUse(RTSP_PORT) || isPortInUse(API_PORT)) {
        emit logMessage(
            QStringLiteral("포트 %1/%2 사용 중. 우리가 띄운 mediamtx 잔존 여부를 확인합니다.")
                .arg(RTSP_PORT).arg(API_PORT),
            2);
        if (reclaimStaleProcess()) {
            waitForPortsFree(3000);
        }
        if (isPortInUse(RTSP_PORT) || isPortInUse(API_PORT)) {
            emit logMessage(
                QStringLiteral("포트 %1/%2 가 다른 프로세스에 점유되어 있습니다(우리 mediamtx 아님). "
                               "임의 종료하지 않습니다. 점유 중인 프로그램을 확인하세요.")
                    .arg(RTSP_PORT).arg(API_PORT),
                3);
            return false;
        }
        emit logMessage(QStringLiteral("포트 정리 완료."), 1);
    }

    m_intentionalStop = false;
    m_process->setProgram(executable);
    m_process->setArguments({configPath()});
    m_process->start();

    if (!m_process->waitForStarted(2000)) {
        emit logMessage(
            QStringLiteral("MediaMTX 시작 실패: %1").arg(m_process->errorString()), 3);
        return false;
    }

    // 기동 헬스 체크: waitForStarted() 는 프로세스 '생성'만 확인한다.
    // config/bind 오류로 즉시 죽는 경우를 잡기 위해, 프로세스가 살아있고
    // API 포트가 실제로 열릴 때까지(최대 4초) 확인한 뒤 성공으로 본다.
    QElapsedTimer health;
    health.start();
    bool healthy = false;
    while (health.elapsed() < 4000) {
        if (m_process->state() != QProcess::Running) {
            break;   // 즉시 종료 = 기동 실패
        }
        if (isPortInUse(API_PORT)) {
            healthy = true;   // API 리스너 open = 정상 기동 확인
            break;
        }
        QThread::msleep(150);
    }

    if (!healthy) {
        emit logMessage(
            QStringLiteral("MediaMTX 기동 확인 실패(즉시 종료 또는 API 포트 미개방). "
                           "config/포트 충돌 가능성 — 위 MediaMTX 로그를 확인하세요."),
            3);
        if (m_process->state() != QProcess::NotRunning) {
            stopProcess();    // 떠 있으나 비정상이면 정리
        } else {
            removePidFile();  // 이미 죽었으면 잔여 PID 기록 정리
        }
        return false;
    }

    const qint64 pid = m_process->processId();
    writePidFile(pid);   // 건강 확인 후에만 기록 → 다음 실행의 고아 회수에 사용
    emit logMessage(
        QStringLiteral("MediaMTX 시작·확인됨 (pid=%1, API/RTSP open, config=%2).")
            .arg(pid).arg(configPath()),
        1);
    return true;
}

void MediaRelayManager::stopProcess()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }

    m_intentionalStop = true;   // finished 핸들러가 크래시로 오인하지 않도록 표시

#if defined(Q_OS_WIN)
    // Windows 콘솔 앱(mediamtx)은 terminate()(WM_CLOSE)에 반응하지 않으므로 곧장 kill.
    m_process->kill();
    m_process->waitForFinished(2000);
#else
    m_process->terminate();
    if (!m_process->waitForFinished(2500)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
#endif

    removePidFile();   // 정상 종료 → 고아 아님. 다음 실행이 죽은 PID 를 오인하지 않도록 제거.
}

bool MediaRelayManager::isPortInUse(quint16 port) const
{
    // 127.0.0.1:<port> 에 잠깐 bind 를 시도한다. 실패하면 누군가 점유 중.
    QTcpServer probe;
    const bool bound = probe.listen(QHostAddress::LocalHost, port);
    if (bound) {
        probe.close();
        return false;
    }
    return true;
}

bool MediaRelayManager::waitForPortsFree(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!isPortInUse(RTSP_PORT) && !isPortInUse(API_PORT)) {
            return true;
        }
        QThread::msleep(150);
    }
    return !isPortInUse(RTSP_PORT) && !isPortInUse(API_PORT);
}

QString MediaRelayManager::pidFilePath() const
{
    const QString dir = QDir::homePath() + QStringLiteral("/.ziilab");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/mediamtx.pid");
}

void MediaRelayManager::writePidFile(qint64 pid)
{
    QSaveFile file(pidFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    file.write(QByteArray::number(pid));
    file.commit();
}

void MediaRelayManager::removePidFile()
{
    QFile::remove(pidFilePath());
}

qint64 MediaRelayManager::readPidFile() const
{
    QFile file(pidFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    bool ok = false;
    const qint64 pid = QString::fromLatin1(file.readAll()).trimmed().toLongLong(&ok);
    return ok ? pid : -1;
}

bool MediaRelayManager::isMediamtxProcess(qint64 pid) const
{
    if (pid <= 0) return false;

    QProcess query;
#if defined(Q_OS_WIN)
    // tasklist 는 해당 PID 가 없으면 "INFO: No tasks..." 를 출력한다(= mediamtx 미포함).
    query.start(QStringLiteral("tasklist"),
                {QStringLiteral("/FI"), QStringLiteral("PID eq %1").arg(pid),
                 QStringLiteral("/FO"), QStringLiteral("CSV"), QStringLiteral("/NH")});
#else
    query.start(QStringLiteral("ps"),
                {QStringLiteral("-p"), QString::number(pid),
                 QStringLiteral("-o"), QStringLiteral("comm=")});
#endif
    if (!query.waitForStarted(1500)) return false;
    if (!query.waitForFinished(2000)) {
        query.kill();
        return false;
    }
    const QString out = QString::fromLocal8Bit(query.readAllStandardOutput());
    return out.contains(QStringLiteral("mediamtx"), Qt::CaseInsensitive);
}

bool MediaRelayManager::killProcessByPid(qint64 pid)
{
    if (pid <= 0) return false;
#if defined(Q_OS_WIN)
    const int rc = QProcess::execute(QStringLiteral("taskkill"),
                                     {QStringLiteral("/F"), QStringLiteral("/PID"),
                                      QString::number(pid)});
#else
    const int rc = QProcess::execute(QStringLiteral("kill"),
                                     {QStringLiteral("-9"), QString::number(pid)});
#endif
    return rc == 0;
}

bool MediaRelayManager::reclaimStaleProcess()
{
    // 우리 프로세스가 살아있으면 회수 대상 아님.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return false;
    }

    const qint64 pid = readPidFile();
    if (pid <= 0) {
        emit logMessage(QStringLiteral("이전 mediamtx PID 기록이 없습니다(회수 대상 없음)."), 0);
        return false;
    }

    // PID 재사용 방지: 기록된 PID 가 지금도 mediamtx 일 때만 종료한다.
    if (!isMediamtxProcess(pid)) {
        emit logMessage(
            QStringLiteral("기록된 PID %1 는 더 이상 mediamtx 가 아닙니다(죽었거나 재사용됨). 종료하지 않습니다.")
                .arg(pid),
            0);
        removePidFile();
        return false;
    }

    emit logMessage(QStringLiteral("우리가 띄웠던 잔존 mediamtx 회수 (pid=%1).").arg(pid), 2);
    const bool ok = killProcessByPid(pid);
    if (ok) {
        removePidFile();
    } else {
        emit logMessage(QStringLiteral("PID %1 종료 실패.").arg(pid), 3);
    }
    return ok;
}
