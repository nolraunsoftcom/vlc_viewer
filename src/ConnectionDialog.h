#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <optional>
#include <cstdint>

struct ConnectionInfo {
    QString channelName;
    QString rtspUrl;
    QString serverAddress;
    uint16_t serverPort;
    QString password;
    bool autoReconnect;
};

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(int channelNumber, QWidget *parent = nullptr);

    static std::optional<ConnectionInfo> getConnectionInfo(QWidget *parent, int channelNumber = 1);

private:
    QLineEdit *m_channelName = nullptr;
    QLineEdit *m_rtspUrl = nullptr;
    QLineEdit *m_serverAddress = nullptr;
    QSpinBox  *m_serverPort = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_autoReconnect = nullptr;

    ConnectionInfo result() const;
};
