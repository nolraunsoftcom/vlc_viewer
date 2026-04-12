#pragma once

#include <QWidget>
#include <QLabel>
#include <QVector>

class VlcWidget;

class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);

    void updateStats(const QVector<VlcWidget *> &viewers);

private:
    QLabel *m_label = nullptr;
};
