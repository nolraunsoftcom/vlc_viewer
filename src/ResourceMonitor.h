#pragma once

#include <QtGlobal>

struct ResourceSnapshot {
    bool systemCpuValid = false;
    bool systemMemoryValid = false;

    double systemCpuPercent = 0.0;
    quint64 systemMemoryUsedBytes = 0;
    quint64 systemMemoryTotalBytes = 0;
};

class ResourceMonitor
{
public:
    ResourceSnapshot sample();

private:
    bool m_hasSystemCpuSample = false;
    quint64 m_lastSystemIdleTicks = 0;
    quint64 m_lastSystemTotalTicks = 0;

    static bool readSystemCpuTicks(quint64 *idleTicks, quint64 *totalTicks);
    static bool readSystemMemory(quint64 *usedBytes, quint64 *totalBytes);
};
