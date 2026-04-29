#pragma once

#include "ResourceMonitor.h"

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
    QLabel *m_streamLabel = nullptr;
    QLabel *m_viewerCpuValueLabel = nullptr;
    QLabel *m_viewerMemoryValueLabel = nullptr;
    QLabel *m_systemCpuValueLabel = nullptr;
    QLabel *m_systemMemoryValueLabel = nullptr;
    ResourceMonitor m_resourceMonitor;
};
