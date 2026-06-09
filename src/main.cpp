#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <vector>
#include <vlc/vlc.h>
#include <cstdlib>
#include <QThreadPool>
#include "MainWindow.h"

static MainWindow *g_mainWindow = nullptr;

static void vlcLogCallback(void *, int level, const libvlc_log_t *, const char *fmt, va_list args)
{
    if (!g_mainWindow) return;

    // VLC 레벨: 0=DEBUG, 2=NOTICE, 3=WARNING, 4=ERROR
    // 의견#1: WARNING 숨김, ERROR 만 표시
    if (level < LIBVLC_ERROR) return;

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);

    // 반복되는 무해한 에러 필터링
    QString msgStr = QString::fromUtf8(buf);
    if (msgStr.contains("deadlock prevented") ||
        msgStr.contains("Timestamp conversion") ||
        msgStr.contains("Could not convert timestamp") ||
        msgStr.contains("picture is too late") ||
        msgStr.contains("late frames, dropping frame") ||
        msgStr.contains("Waiting for VPS/SPS/PPS") ||
        // MP4 muxer 의 live RTSP 녹화 중 양성 경고들
        msgStr.contains("i_length") ||
        msgStr.contains("pts == 0") ||
        msgStr.contains("Track id ") ||
        msgStr.contains("Missing or unsupported sample") ||
        msgStr.contains("late buffer for mux input") ||
        msgStr.contains("buffer deadlock prevented") ||
        msgStr.contains("unsupported control query", Qt::CaseInsensitive) ||
        msgStr.contains("cannot peek") ||
        msgStr.contains("no more input streams for this mux") ||
        msgStr.contains("Missing video bitrate") ||
        msgStr.contains("Failed to teardown RTSP session") ||
        msgStr.contains("Cseq mismatch") ||
        msgStr.contains("only real/helix rtsp servers supported for now") ||
        msgStr.contains("Your input can't be opened") ||
        msgStr.contains("VLC is unable to open the MRL") ||
        msgStr.contains("sout_AccessOutWrite")) {
        return;
    }

    LogLevel logLevel = LogLevel::ERROR;  // 위 필터로 ERROR 만 통과
    QString msg = QString("VLC: %1").arg(QString::fromUtf8(buf).trimmed());

    QMetaObject::invokeMethod(g_mainWindow, [=]() {
        g_mainWindow->appendLog(msg, logLevel);
    }, Qt::QueuedConnection);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("영상관리시스템"));
    app.setApplicationDisplayName(QStringLiteral("영상관리시스템"));
    app.setWindowIcon(QIcon(":/logo.png"));
    app.setStyleSheet(
        "QToolTip { color: #1f1f1f; background-color: #ffffe1; "
        "border: 1px solid #767676; padding: 3px; }");

    QString pluginPath;
    const QDir appDir(QApplication::applicationDirPath());

#if defined(__APPLE__)
    const QFileInfo bundledPlugins(appDir.filePath("../Frameworks/plugins"));

    if (bundledPlugins.exists() && bundledPlugins.isDir()) {
        pluginPath = bundledPlugins.canonicalFilePath();
    } else {
        pluginPath = QStringLiteral("/Applications/VLC.app/Contents/MacOS/plugins");
    }
#elif defined(_WIN32)
    const QFileInfo bundledPlugins(appDir.filePath("plugins"));
    if (bundledPlugins.exists() && bundledPlugins.isDir()) {
        pluginPath = bundledPlugins.canonicalFilePath();
    }
#endif

    if (!pluginPath.isEmpty()) {
        qputenv("VLC_PLUGIN_PATH", QDir::toNativeSeparators(pluginPath).toUtf8());
    }

    // VLC 인스턴스 생성 — 의견#3(순정 지향) 기준.
    // 제거: --clock-jitter=0 / --clock-synchro=0 (지터보상·재동기화를 꺼서 지연이 단조
    //       누적되고 한번 밀리면 고착되던 주범. 순정 기본값으로 복원), --avcodec-hurry-up /
    //       --avcodec-fast (화질저하 트레이드오프, 비순정).
    std::vector<const char *> vlcArgs = {
        "--reset-plugins-cache",
        "--network-caching=1000",   // 의견#5: 무선 깨짐 방지용 캐시 상향
        "--live-caching=1500",      // VLC 기본값(1500)에 맞춤 — live 버퍼 부족으로 인한 프레임 드롭/뭉개짐 완화
        "--drop-late-frames",       // 늦은 프레임은 버려 지연 누적 차단 (순정도 사용하는 레버)
        "--no-audio",               // 의견#4: 오디오 미사용
    };
#if defined(__APPLE__)
    vlcArgs.push_back("--no-videotoolbox");
#endif

    libvlc_instance_t *vlcInstance = libvlc_new(
        static_cast<int>(vlcArgs.size()),
        vlcArgs.data());
    if (!vlcInstance) {
        QString detail = pluginPath.isEmpty()
            ? QStringLiteral("VLC plugin path not found next to the executable.")
            : QString("VLC plugin path: %1").arg(QDir::toNativeSeparators(pluginPath));
        QMessageBox::critical(
            nullptr,
            QStringLiteral("영상관리시스템"),
            QString("libVLC initialization failed.\n\n%1\n\n"
                    "On Windows, the app folder must include the VLC runtime DLLs "
                    "and a plugins directory.")
                .arg(detail));
        return 1;
    }

    // VLC 내부 로그 캡처
    libvlc_log_set(vlcInstance, vlcLogCallback, nullptr);

    auto *window = new MainWindow(vlcInstance);
    g_mainWindow = window;
    window->show();

    int result = app.exec();

    // 1. 로그 콜백 비활성화
    g_mainWindow = nullptr;

    // 2. MainWindow + 모든 VlcWidget 먼저 파괴 (stop/cleanupPlayer 실행)
    delete window;

    // 3. 백그라운드 VLC 정리 태스크 완료 대기
    QThreadPool::globalInstance()->waitForDone(5000);

    // 4. 마지막으로 VLC 인스턴스 해제
    libvlc_release(vlcInstance);
    return result;
}
