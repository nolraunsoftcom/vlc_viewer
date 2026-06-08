#include "ConnectionDialog.h"
#include "MediaRelayManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QUrl>

static const QString DIALOG_STYLE = R"(
QDialog {
    background-color: #ffffff;
}
QGroupBox {
    color: #333;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    margin-top: 8px;
    padding-top: 8px;
    font-size: 11px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 4px;
    left: 8px;
}
QLabel {
    color: #222;
    font-size: 12px;
}
QLineEdit {
    background-color: #ffffff;
    border: 1px solid #bdbdbd;
    color: #222;
    padding: 4px 6px;
    border-radius: 3px;
    font-size: 12px;
}
QLineEdit:focus {
    border-color: #0078d4;
}
QLineEdit:disabled {
    background-color: #f3f3f3;
    color: #999;
    border-color: #d0d0d0;
}
QSpinBox {
    background-color: #ffffff;
    border: 1px solid #bdbdbd;
    color: #222;
    padding: 4px 6px;
    border-radius: 3px;
    font-size: 12px;
}
QSpinBox:disabled {
    background-color: #f3f3f3;
    color: #999;
    border-color: #d0d0d0;
}
QCheckBox {
    color: #222;
    font-size: 12px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background-color: #ffffff;
    border: 1px solid #bdbdbd;
    border-radius: 2px;
}
QCheckBox::indicator:hover {
    border-color: #0078d4;
}
QCheckBox::indicator:checked {
    background-color: #0078d4;
    border-color: #0078d4;
}
QCheckBox::indicator:disabled {
    background-color: #f3f3f3;
    border-color: #d0d0d0;
}
QPushButton {
    color: #222;
    background-color: #f7f7f7;
    border: 1px solid #bdbdbd;
    padding: 5px 16px;
    font-size: 12px;
    border-radius: 3px;
}
QPushButton:hover {
    background-color: #e8f2ff;
    border-color: #0078d4;
}
QPushButton:default {
    border-color: #0078d4;
}
)";

ConnectionDialog::ConnectionDialog(int channelNumber, QWidget *parent)
    : QDialog(parent)
{
    setupUi(ConnectionInfo{
        QString("Camera %1").arg(channelNumber),
        "rtsp://169.254.4.1:8900/live",
        true,
        true,
        "rtsp://169.254.4.1:8900/live",
        QString("voxl%1").arg(channelNumber)
    }, "채널 추가");
}

ConnectionDialog::ConnectionDialog(const ConnectionInfo &initialInfo,
                                   const QString &windowTitle,
                                   QWidget *parent)
    : QDialog(parent)
{
    setupUi(initialInfo, windowTitle);
}

void ConnectionDialog::setupUi(const ConnectionInfo &initialInfo, const QString &windowTitle)
{
    setWindowTitle(windowTitle);
    setModal(true);
    setMinimumWidth(520);
    setStyleSheet(DIALOG_STYLE);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // === 채널 정보 그룹 ===
    auto *channelGroup = new QGroupBox("채널 정보", this);
    auto *channelForm = new QFormLayout(channelGroup);
    channelForm->setContentsMargins(10, 16, 10, 10);
    channelForm->setSpacing(8);
    channelForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_channelName = new QLineEdit(initialInfo.channelName, channelGroup);
    m_channelName->setMinimumWidth(300);
    channelForm->addRow("채널 이름:", m_channelName);

    const QString sourceUrl = initialInfo.sourceUrl.trimmed().isEmpty()
        ? initialInfo.rtspUrl.trimmed()
        : initialInfo.sourceUrl.trimmed();
    m_rtspUrl = new QLineEdit(sourceUrl, channelGroup);
    m_rtspUrl->setMinimumWidth(300);
    channelForm->addRow("원본 RTSP URL:", m_rtspUrl);

    mainLayout->addWidget(channelGroup);

    // === 옵션 그룹 ===
    auto *optionsGroup = new QGroupBox("옵션", this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setContentsMargins(10, 16, 10, 10);

    m_relayEnabled = new QCheckBox("MediaMTX relay 사용", optionsGroup);
    m_relayEnabled->setChecked(initialInfo.relayEnabled);
    optionsLayout->addWidget(m_relayEnabled);

    auto *relayPathLayout = new QHBoxLayout();
    relayPathLayout->setContentsMargins(0, 0, 0, 0);
    relayPathLayout->setSpacing(8);

    auto *relayPathLabel = new QLabel("Relay path:", optionsGroup);
    relayPathLabel->setMinimumWidth(82);
    relayPathLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    relayPathLayout->addWidget(relayPathLabel);

    const QString relayPath = initialInfo.relayPath.trimmed().isEmpty()
        ? QStringLiteral("voxl1")
        : MediaRelayManager::normalizeRelayPath(initialInfo.relayPath);
    m_relayPath = new QLineEdit(relayPath, optionsGroup);
    m_relayPath->setPlaceholderText(QStringLiteral("voxl1"));
    relayPathLayout->addWidget(m_relayPath, 1);
    optionsLayout->addLayout(relayPathLayout);

    m_autoReconnect = new QCheckBox("자동 재연결", optionsGroup);
    m_autoReconnect->setChecked(initialInfo.autoReconnect);
    optionsLayout->addWidget(m_autoReconnect);

    auto updateRelayUi = [this, relayPathLabel]() {
        const bool enabled = m_relayEnabled && m_relayEnabled->isChecked();
        if (m_relayPath) m_relayPath->setEnabled(enabled);
        if (relayPathLabel) relayPathLabel->setEnabled(enabled);
    };
    connect(m_relayEnabled, &QCheckBox::toggled, this, [updateRelayUi](bool) {
        updateRelayUi();
    });
    updateRelayUi();

    mainLayout->addWidget(optionsGroup);

    // === 버튼 ===
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText("확인");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("취소");

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

ConnectionInfo ConnectionDialog::result() const
{
    const QString sourceUrl = m_rtspUrl->text().trimmed();
    const bool relayEnabled = m_relayEnabled && m_relayEnabled->isChecked();
    const QString relayPath = m_relayPath
        ? MediaRelayManager::normalizeRelayPath(m_relayPath->text())
        : QString();

    return ConnectionInfo{
        m_channelName->text().trimmed(),
        sourceUrl,
        m_autoReconnect->isChecked(),
        relayEnabled,
        sourceUrl,
        relayPath
    };
}

bool ConnectionDialog::isRtspUrlAllowed(const QString &url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    return parsed.isValid()
        && (scheme == QStringLiteral("rtsp") || scheme == QStringLiteral("rtsps"))
        && !parsed.host().isEmpty();
}

void ConnectionDialog::accept()
{
    const ConnectionInfo info = result();
    if (info.channelName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("채널 정보"),
                             QStringLiteral("채널 이름을 입력하세요."));
        m_channelName->setFocus();
        return;
    }

    if (!isRtspUrlAllowed(info.sourceUrl)) {
        QMessageBox::warning(
            this,
            QStringLiteral("RTSP URL"),
            QStringLiteral("RTSP URL은 rtsp:// 또는 rtsps:// 형식으로 입력하세요."));
        m_rtspUrl->setFocus();
        return;
    }

    if (info.relayEnabled && !MediaRelayManager::isValidRelayPath(info.relayPath)) {
        QMessageBox::warning(
            this,
            QStringLiteral("MediaMTX relay"),
            QStringLiteral("Relay path는 영문/숫자/점/하이픈/밑줄만 사용할 수 있습니다."));
        m_relayPath->setFocus();
        return;
    }

    m_channelName->setText(info.channelName);
    m_rtspUrl->setText(info.sourceUrl);
    if (m_relayPath) {
        m_relayPath->setText(info.relayPath);
    }
    QDialog::accept();
}

std::optional<ConnectionInfo> ConnectionDialog::getConnectionInfo(QWidget *parent, int channelNumber)
{
    ConnectionDialog dlg(channelNumber, parent);
    if (dlg.exec() == QDialog::Accepted) {
        ConnectionInfo info = dlg.result();
        if (!info.channelName.isEmpty() && !info.sourceUrl.isEmpty()) {
            return info;
        }
    }
    return std::nullopt;
}

std::optional<ConnectionInfo> ConnectionDialog::getConnectionInfo(QWidget *parent,
                                                                  const ConnectionInfo &initialInfo,
                                                                  const QString &windowTitle)
{
    ConnectionDialog dlg(initialInfo, windowTitle, parent);
    if (dlg.exec() == QDialog::Accepted) {
        ConnectionInfo info = dlg.result();
        if (!info.channelName.isEmpty() && !info.sourceUrl.isEmpty()) {
            return info;
        }
    }
    return std::nullopt;
}
