#include "ConnectionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

static const QString DIALOG_STYLE = R"(
QDialog {
    background-color: #2a2a2a;
}
QGroupBox {
    color: #888;
    border: 1px solid #444;
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
    color: #ccc;
    font-size: 12px;
}
QLineEdit {
    background-color: #1a1a1a;
    border: 1px solid #444;
    color: white;
    padding: 4px 6px;
    border-radius: 3px;
    font-size: 12px;
}
QLineEdit:focus {
    border-color: #4a9eff;
}
QLineEdit:disabled {
    background-color: #222;
    color: #555;
    border-color: #333;
}
QSpinBox {
    background-color: #1a1a1a;
    border: 1px solid #444;
    color: white;
    padding: 4px 6px;
    border-radius: 3px;
    font-size: 12px;
}
QSpinBox:disabled {
    background-color: #222;
    color: #555;
    border-color: #333;
}
QCheckBox {
    color: #ccc;
    font-size: 12px;
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background-color: #1a1a1a;
    border: 1px solid #444;
    border-radius: 2px;
}
QCheckBox::indicator:checked {
    background-color: #4a9eff;
    border-color: #4a9eff;
    border-width: 2px;
    border-style: solid;
    border-top: none;
    border-right: none;
    width: 10px;
    height: 6px;
    border-radius: 0px;
    background-color: transparent;
    border-color: white;
}
QPushButton {
    color: white;
    background-color: #444;
    border: 1px solid #555;
    padding: 5px 16px;
    font-size: 12px;
    border-radius: 3px;
}
QPushButton:hover {
    background-color: #555;
}
QPushButton:default {
    border-color: #4a9eff;
}
)";

ConnectionDialog::ConnectionDialog(int channelNumber, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("채널 추가");
    setModal(true);
    setMinimumWidth(400);
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

    m_channelName = new QLineEdit(QString("Camera %1").arg(channelNumber), channelGroup);
    channelForm->addRow("채널 이름:", m_channelName);

    m_rtspUrl = new QLineEdit("rtsp://10.1.100.30:8904/live", channelGroup);
    channelForm->addRow("RTSP URL:", m_rtspUrl);

    mainLayout->addWidget(channelGroup);

    // === 옵션 그룹 ===
    auto *optionsGroup = new QGroupBox("옵션", this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    optionsLayout->setContentsMargins(10, 16, 10, 10);

    m_autoReconnect = new QCheckBox("자동 재연결", optionsGroup);
    m_autoReconnect->setChecked(true);
    optionsLayout->addWidget(m_autoReconnect);

    mainLayout->addWidget(optionsGroup);

    // === 버튼 ===
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttonBox->button(QDialogButtonBox::Ok)->setText("확인");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("취소");

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(buttonBox);
}

ConnectionInfo ConnectionDialog::result() const
{
    return ConnectionInfo{
        m_channelName->text(),
        m_rtspUrl->text(),
        m_autoReconnect->isChecked()
    };
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
