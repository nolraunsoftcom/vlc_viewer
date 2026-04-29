#include "StatusBar.h"
#include "VlcWidget.h"
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace {

constexpr double BYTES_PER_GB = 1024.0 * 1024.0 * 1024.0;

QString formatCpu(bool valid, double percent)
{
    if (!valid) return QStringLiteral("--");
    return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
}

QString formatMemoryGb(bool valid, quint64 bytes)
{
    if (!valid) return QStringLiteral("--");
    return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / BYTES_PER_GB, 0, 'f', 2);
}

QString formatSystemMemoryGb(const ResourceSnapshot &snapshot)
{
    if (!snapshot.systemMemoryValid) return QStringLiteral("--");
    return QStringLiteral("%1 / %2 GB")
        .arg(static_cast<double>(snapshot.systemMemoryUsedBytes) / BYTES_PER_GB, 0, 'f', 2)
        .arg(static_cast<double>(snapshot.systemMemoryTotalBytes) / BYTES_PER_GB, 0, 'f', 2);
}

QLabel *createResourceLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setStyleSheet("color: #9a9a9a; font-size: 11px; background-color: transparent;");
    return label;
}

QLabel *createResourceValueLabel(const QFont &font, const QString &widthSample, QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setFont(font);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedWidth(QFontMetrics(font).horizontalAdvance(widthSample) + 4);
    label->setStyleSheet("color: #b8b8b8; background-color: transparent;");
    return label;
}

void setLabelTextIfChanged(QLabel *label, const QString &text)
{
    if (label && label->text() != text) {
        label->setText(text);
    }
}

}  // namespace

StatusBar::StatusBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(46);
    setObjectName("statusBar");
    setStyleSheet("#statusBar { background-color: #1a1a1a; border: none; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 3, 10, 3);
    layout->setSpacing(0);

    m_streamLabel = new QLabel(
        QStringLiteral("Connected: 0/0 | Bitrate: 0.0 Mbps | FPS: 0.0 | Dropped: 0"),
        this);
    m_streamLabel->setStyleSheet(
        "color: #ccc; font-size: 11px; background-color: transparent;");
    layout->addWidget(m_streamLabel);

    auto fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    fixedFont.setPixelSize(11);

    auto *resourceRow = new QWidget(this);
    resourceRow->setStyleSheet("background-color: transparent;");
    auto *resourceLayout = new QHBoxLayout(resourceRow);
    resourceLayout->setContentsMargins(0, 0, 0, 0);
    resourceLayout->setSpacing(4);

    m_viewerCpuValueLabel =
        createResourceValueLabel(fixedFont, QStringLiteral("100.0%"), resourceRow);
    m_viewerMemoryValueLabel =
        createResourceValueLabel(fixedFont, QStringLiteral("99.99 GB"), resourceRow);
    m_systemCpuValueLabel =
        createResourceValueLabel(fixedFont, QStringLiteral("100.0%"), resourceRow);
    m_systemMemoryValueLabel =
        createResourceValueLabel(fixedFont, QStringLiteral("999.99 / 999.99 GB"), resourceRow);

    resourceLayout->addWidget(createResourceLabel(QStringLiteral("Viewer CPU:"), resourceRow));
    resourceLayout->addWidget(m_viewerCpuValueLabel);
    resourceLayout->addWidget(createResourceLabel(QStringLiteral("| Viewer RAM:"), resourceRow));
    resourceLayout->addWidget(m_viewerMemoryValueLabel);
    resourceLayout->addWidget(createResourceLabel(QStringLiteral("| PC CPU:"), resourceRow));
    resourceLayout->addWidget(m_systemCpuValueLabel);
    resourceLayout->addWidget(createResourceLabel(QStringLiteral("| PC RAM:"), resourceRow));
    resourceLayout->addWidget(m_systemMemoryValueLabel);
    resourceLayout->addStretch();

    layout->addWidget(resourceRow);
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

    const QString streamText =
        QStringLiteral("Connected: %1/%2 | Bitrate: %3 Mbps | FPS: %4 | Dropped: %5")
        .arg(connected)
        .arg(total)
        .arg(avgBitrateMbps, 0, 'f', 1)
        .arg(avgFps, 0, 'f', 1)
        .arg(totalDropped);
    if (m_streamLabel->text() != streamText) {
        m_streamLabel->setText(streamText);
    }

    const ResourceSnapshot resources = m_resourceMonitor.sample();
    setLabelTextIfChanged(m_viewerCpuValueLabel,
                          formatCpu(resources.processCpuValid, resources.processCpuPercent));
    setLabelTextIfChanged(m_viewerMemoryValueLabel,
                          formatMemoryGb(resources.processMemoryValid,
                                         resources.processMemoryBytes));
    setLabelTextIfChanged(m_systemCpuValueLabel,
                          formatCpu(resources.systemCpuValid, resources.systemCpuPercent));
    setLabelTextIfChanged(m_systemMemoryValueLabel, formatSystemMemoryGb(resources));
}
