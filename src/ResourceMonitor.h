#pragma once

#include <QtGlobal>
#include <chrono>

struct ResourceSnapshot {
    bool processCpuValid = false;
    bool processMemoryValid = false;
    bool systemCpuValid = false;
    bool systemMemoryValid = false;

    double processCpuPercent = 0.0;
    quint64 processMemoryBytes = 0;
    double systemCpuPercent = 0.0;
    quint64 systemMemoryUsedBytes = 0;
    quint64 systemMemoryTotalBytes = 0;
};

class ResourceMonitor
{
public:
    ResourceMonitor();

    ResourceSnapshot sample();

private:
    int m_logicalCpuCount = 1;
    bool m_hasProcessCpuSample = false;
    bool m_hasSystemCpuSample = false;
    std::chrono::steady_clock::time_point m_lastProcessSampleTime;
    double m_lastProcessCpuSeconds = 0.0;
    quint64 m_lastSystemIdleTicks = 0;
    quint64 m_lastSystemTotalTicks = 0;

    static int detectLogicalCpuCount();
    static double readProcessCpuSeconds(bool *ok);
    static quint64 readProcessMemoryBytes(bool *ok);
    static bool readSystemCpuTicks(quint64 *idleTicks, quint64 *totalTicks);
    static bool readSystemMemory(quint64 *usedBytes, quint64 *totalBytes);
};
