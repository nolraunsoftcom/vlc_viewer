#include "StatusBar.h"
#include "VlcWidget.h"
#include <QHBoxLayout>
#include <QPalette>

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(24);
    setStyleSheet("StatusBar { border-top: 1px solid #333; }");

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(0);

    m_label = new QLabel("Connected: 0/0 | Bitrate: 0.0 Mbps | FPS: 0.0 | Dropped: 0", this);
    m_label->setStyleSheet("color: #ccc; font-size: 11px; background-color: #1a1a1a;");
    layout->addWidget(m_label);
    layout->addStretch();
}

void StatusBar::updateStats(const QVector<VlcWidget *> &viewers)
{
    int total = viewers.size();
    int connected = 0;
    double totalBitrate = 0.0;
    double totalFps = 0.0;
    int totalDropped = 0;

    for (auto *viewer : viewers) {
        VlcWidget::Stats s = viewer->getStats();
        if (s.playing) {
            ++connected;
            totalBitrate += s.bitrate_kbps;
            totalFps += s.fps;
            totalDropped += s.lost_pictures;
        }
    }

    double avgFps = connected > 0 ? totalFps / connected : 0.0;
    double avgBitrateMbps = connected > 0 ? totalBitrate / connected / 1000.0 : 0.0;

    m_label->setText(
        QString("Connected: %1/%2 | Bitrate: %3 Mbps | FPS: %4 | Dropped: %5")
            .arg(connected)
            .arg(total)
            .arg(avgBitrateMbps, 0, 'f', 1)
            .arg(avgFps, 0, 'f', 1)
            .arg(totalDropped));
}
