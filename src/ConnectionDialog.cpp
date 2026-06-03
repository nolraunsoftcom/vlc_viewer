#include "ConnectionDialog.h"

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
        "rtsp://10.1.100.30:8904/live",
        true
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

    m_rtspUrl = new QLineEdit(initialInfo.rtspUrl, channelGroup);
    m_rtspUrl->setMinimumWidth(300);
    channelForm->addRow("RTSP URL:", m_rtspUrl);

    mainLayout->addWidget(channelGroup);

    // === 옵션 그룹 ===
    auto *optionsGroup = new QGroupBox("옵션", this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setContentsMargins(10, 16, 10, 10);

    m_autoReconnect = new QCheckBox("자동 재연결", optionsGroup);
    m_autoReconnect->setChecked(initialInfo.autoReconnect);
    optionsLayout->addWidget(m_autoReconnect);

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
    return ConnectionInfo{
        m_channelName->text().trimmed(),
        m_rtspUrl->text().trimmed(),
        m_autoReconnect->isChecked()
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

    if (!isRtspUrlAllowed(info.rtspUrl)) {
        QMessageBox::warning(
            this,
            QStringLiteral("RTSP URL"),
            QStringLiteral("RTSP URL은 rtsp:// 또는 rtsps:// 형식으로 입력하세요."));
        m_rtspUrl->setFocus();
        return;
    }

    m_channelName->setText(info.channelName);
    m_rtspUrl->setText(info.rtspUrl);
    QDialog::accept();
}

std::optional<ConnectionInfo> ConnectionDialog::getConnectionInfo(QWidget *parent, int channelNumber)
{
    ConnectionDialog dlg(channelNumber, parent);
    if (dlg.exec() == QDialog::Accepted) {
        ConnectionInfo info = dlg.result();
        if (!info.channelName.isEmpty() && !info.rtspUrl.isEmpty()) {
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
        if (!info.channelName.isEmpty() && !info.rtspUrl.isEmpty()) {
            return info;
        }
    }
    return std::nullopt;
}
