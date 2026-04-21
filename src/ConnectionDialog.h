#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <optional>

struct ConnectionInfo {
    QString channelName;
    QString rtspUrl;
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
    QCheckBox *m_autoReconnect = nullptr;

    ConnectionInfo result() const;
};
