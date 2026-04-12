#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <vlc/vlc.h>
#include <cstdlib>
#include <QThreadPool>
#include "MainWindow.h"

static MainWindow *g_mainWindow = nullptr;

static void vlcLogCallback(void *, int level, const libvlc_log_t *, const char *fmt, va_list args)
{
    if (!g_mainWindow) return;

    // VLC 레벨: 0=DEBUG, 1=NOTICE, 2=WARNING, 3=ERROR
    if (level < 2) return;

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);

    // 반복되는 무해한 에러 필터링
    QString msgStr = QString::fromUtf8(buf);
    if (msgStr.contains("deadlock prevented") ||
        msgStr.contains("Timestamp conversion") ||
        msgStr.contains("Could not convert timestamp") ||
        msgStr.contains("picture is too late")) {
        return;
    }

    LogLevel logLevel = (level >= 3) ? LogLevel::ERROR : LogLevel::WARN;
    QString msg = QString("VLC: %1").arg(QString::fromUtf8(buf).trimmed());

    QMetaObject::invokeMethod(g_mainWindow, [=]() {
        g_mainWindow->appendLog(msg, logLevel);
    }, Qt::QueuedConnection);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#if defined(__APPLE__)
    QString pluginPath;
    const QDir appDir(QApplication::applicationDirPath());
    const QFileInfo bundledPlugins(appDir.filePath("../Frameworks/plugins"));

    if (bundledPlugins.exists() && bundledPlugins.isDir()) {
        pluginPath = bundledPlugins.canonicalFilePath();
    } else {
        pluginPath = QStringLiteral("/Applications/VLC.app/Contents/MacOS/plugins");
    }

    setenv("VLC_PLUGIN_PATH", pluginPath.toUtf8().constData(), 1);
#endif

    // VLC 인스턴스 생성 (저지연 설정)
    const char *vlcArgs[] = {
        "--reset-plugins-cache",
        "--network-caching=300",
        "--clock-jitter=5000",
        "--clock-synchro=0",
        "--avcodec-skiploopfilter=4",
        "--avcodec-hurry-up",
        "--avcodec-fast",
        "--drop-late-frames",
        "--skip-frames",
        "--no-audio",
        "--no-videotoolbox",
        "--no-ts-trust",
    };
    int vlcArgCount = sizeof(vlcArgs) / sizeof(vlcArgs[0]);
    libvlc_instance_t *vlcInstance = libvlc_new(vlcArgCount, vlcArgs);
    if (!vlcInstance) {
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
