#include "ChannelInfoDialog.h"

#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);

    const double kib = static_cast<double>(bytes) / 1024.0;
    if (kib < 1024.0) return QStringLiteral("%1 KiB").arg(kib, 0, 'f', 0);

    const double mib = kib / 1024.0;
    if (mib < 1024.0) return QStringLiteral("%1 MiB").arg(mib, 0, 'f', 1);

    return QStringLiteral("%1 GiB").arg(mib / 1024.0, 0, 'f', 2);
}

QString formatBitrate(double kbps)
{
    if (kbps >= 1000.0) {
        return QStringLiteral("%1 Mb/s").arg(kbps / 1000.0, 0, 'f', 2);
    }
    return QStringLiteral("%1 kb/s").arg(kbps, 0, 'f', 0);
}

QString formatFrames(int frames)
{
    return QStringLiteral("%1 프레임").arg(frames);
}

QString formatBlocks(int blocks)
{
    return QStringLiteral("%1 블록").arg(blocks);
}

QString formatBuffers(int buffers)
{
    return QStringLiteral("%1 버퍼").arg(buffers);
}

QString formatFps(double fps)
{
    return QStringLiteral("%1 fps").arg(fps, 0, 'f', 1);
}

void setTextIfChanged(QTreeWidgetItem *item, int column, const QString &text)
{
    if (item && item->text(column) != text) {
        item->setText(column, text);
    }
}

} // namespace

ChannelInfoDialog::ChannelInfoDialog(VlcWidget *viewer, QWidget *parent)
    : QDialog(parent)
    , m_viewer(viewer)
{
    setMinimumSize(560, 520);
    setWindowTitle(QStringLiteral("채널 정보"));
    setStyleSheet(
        "QDialog { background-color: #1a1a1a; color: #ddd; }"
        "QTabWidget::pane { border: 1px solid #333; background-color: #1a1a1a; }"
        "QTabBar::tab { background-color: #222; color: #aaa; padding: 7px 18px; }"
        "QTabBar::tab:selected { background-color: #1a1a1a; color: white; border-bottom: 2px solid #4a9eff; }"
        "QTreeWidget { background-color: #181818; color: #ddd; border: none; alternate-background-color: #202020; }"
        "QTreeWidget::item { padding: 3px 2px; }"
        "QHeaderView::section { background-color: #222; color: #aaa; border: none; padding: 5px; }"
        "QLabel { color: #aaa; background-color: transparent; }");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *tabs = new QTabWidget(this);
    auto *statsPage = new QWidget(tabs);
    auto *statsLayout = new QVBoxLayout(statsPage);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(8);

    m_statsTree = new QTreeWidget(statsPage);
    m_statsTree->setColumnCount(2);
    m_statsTree->setHeaderLabels(QStringList{QStringLiteral("항목"), QStringLiteral("값")});
    m_statsTree->setAlternatingRowColors(true);
    m_statsTree->setRootIsDecorated(true);
    m_statsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_statsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    auto *audioGroup = addGroup(QStringLiteral("오디오"));
    addMetric(audioGroup, QStringLiteral("audio.decoded"), QStringLiteral("디코드"));
    addMetric(audioGroup, QStringLiteral("audio.played"), QStringLiteral("재생"));
    addMetric(audioGroup, QStringLiteral("audio.lost"), QStringLiteral("손실"));

    auto *videoGroup = addGroup(QStringLiteral("비디오"));
    addMetric(videoGroup, QStringLiteral("video.decoded"), QStringLiteral("디코드"));
    addMetric(videoGroup, QStringLiteral("video.displayed"), QStringLiteral("출력"));
    addMetric(videoGroup, QStringLiteral("video.lost"), QStringLiteral("손실"));
    addMetric(videoGroup, QStringLiteral("video.output_fps"), QStringLiteral("출력 FPS"));
    addMetric(videoGroup, QStringLiteral("video.nominal_fps"), QStringLiteral("트랙 FPS"));
    addMetric(videoGroup, QStringLiteral("video.recent_lost"), QStringLiteral("최근 손실"));

    auto *inputGroup = addGroup(QStringLiteral("입력/읽기"));
    addMetric(inputGroup, QStringLiteral("input.read_bytes"), QStringLiteral("미디어 데이터 크기"));
    addMetric(inputGroup, QStringLiteral("input.bitrate"), QStringLiteral("입력 비트 전송속도"));
    addMetric(inputGroup, QStringLiteral("demux.read_bytes"), QStringLiteral("Demux한 데이터 크기"));
    addMetric(inputGroup, QStringLiteral("demux.bitrate"), QStringLiteral("내용물 비트 전송속도"));
    addMetric(inputGroup, QStringLiteral("demux.corrupted"), QStringLiteral("버림 (깨졌음)"));
    addMetric(inputGroup, QStringLiteral("demux.discontinuity"), QStringLiteral("누락 (중지됨)"));
    addMetric(inputGroup, QStringLiteral("demux.recent_corrupted"), QStringLiteral("최근 버림"));
    addMetric(inputGroup, QStringLiteral("demux.recent_discontinuity"), QStringLiteral("최근 누락"));

    auto *outputGroup = addGroup(QStringLiteral("스트림 출력"));
    addMetric(outputGroup, QStringLiteral("stream.sent_packets"), QStringLiteral("전송 패킷"));
    addMetric(outputGroup, QStringLiteral("stream.sent_bytes"), QStringLiteral("전송 바이트"));
    addMetric(outputGroup, QStringLiteral("stream.bitrate"), QStringLiteral("전송 비트 전송속도"));

    m_statsTree->expandAll();
    statsLayout->addWidget(m_statsTree, 1);

    m_locationLabel = new QLabel(statsPage);
    m_locationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statsLayout->addWidget(m_locationLabel);

    tabs->addTab(statsPage, QStringLiteral("통계"));
    layout->addWidget(tabs);

    refresh();
}

QTreeWidgetItem *ChannelInfoDialog::addGroup(const QString &title)
{
    auto *group = new QTreeWidgetItem(m_statsTree, QStringList{title, QString()});
    group->setFirstColumnSpanned(true);
    return group;
}

void ChannelInfoDialog::addMetric(QTreeWidgetItem *group, const QString &key, const QString &label)
{
    auto *item = new QTreeWidgetItem(group, QStringList{label, QStringLiteral("--")});
    m_valueItems.insert(key, item);
}

void ChannelInfoDialog::setMetric(const QString &key, const QString &value)
{
    setTextIfChanged(m_valueItems.value(key, nullptr), 1, value);
}

void ChannelInfoDialog::refresh()
{
    if (!m_viewer) {
        setWindowTitle(QStringLiteral("채널 정보"));
        if (m_locationLabel) {
            m_locationLabel->setText(QStringLiteral("위치: --"));
        }
        return;
    }

    setWindowTitle(QStringLiteral("채널 정보 - %1").arg(m_viewer->name()));
    if (m_locationLabel) {
        m_locationLabel->setText(QStringLiteral("위치: %1").arg(m_viewer->url()));
    }

    const VlcWidget::Stats stats = m_viewer->getStats();

    setMetric(QStringLiteral("audio.decoded"), formatBlocks(stats.decoded_audio));
    setMetric(QStringLiteral("audio.played"), formatBuffers(stats.played_abuffers));
    setMetric(QStringLiteral("audio.lost"), formatBuffers(stats.lost_abuffers));

    setMetric(QStringLiteral("video.decoded"), formatBlocks(stats.decoded_video));
    setMetric(QStringLiteral("video.displayed"), formatFrames(stats.displayed_pictures));
    setMetric(QStringLiteral("video.lost"), formatFrames(stats.lost_pictures));
    setMetric(QStringLiteral("video.output_fps"), formatFps(stats.output_fps));
    setMetric(QStringLiteral("video.nominal_fps"), formatFps(stats.nominal_fps));
    setMetric(QStringLiteral("video.recent_lost"), formatFrames(stats.lost_delta));

    setMetric(QStringLiteral("input.read_bytes"), formatBytes(stats.read_bytes));
    setMetric(QStringLiteral("input.bitrate"), formatBitrate(stats.input_bitrate_kbps));
    setMetric(QStringLiteral("demux.read_bytes"), formatBytes(stats.demux_read_bytes));
    setMetric(QStringLiteral("demux.bitrate"), formatBitrate(stats.demux_bitrate_kbps));
    setMetric(QStringLiteral("demux.corrupted"), QString::number(stats.demux_corrupted));
    setMetric(QStringLiteral("demux.discontinuity"), QString::number(stats.demux_discontinuity));
    setMetric(QStringLiteral("demux.recent_corrupted"), QString::number(stats.corrupted_delta));
    setMetric(QStringLiteral("demux.recent_discontinuity"), QString::number(stats.discontinuity_delta));

    setMetric(QStringLiteral("stream.sent_packets"), QString::number(stats.sent_packets));
    setMetric(QStringLiteral("stream.sent_bytes"), formatBytes(stats.sent_bytes));
    setMetric(QStringLiteral("stream.bitrate"), formatBitrate(stats.send_bitrate_kbps));
}
