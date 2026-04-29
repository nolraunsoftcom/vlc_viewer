#include "ResourceMonitor.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QThread>
#include <algorithm>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#elif defined(__unix__)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {

double timevalToSeconds(long seconds, long microseconds)
{
    return static_cast<double>(seconds) + static_cast<double>(microseconds) / 1000000.0;
}

#if defined(_WIN32)
quint64 fileTimeToUInt64(const FILETIME &fileTime)
{
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}
#endif

}  // namespace

ResourceMonitor::ResourceMonitor()
    : m_logicalCpuCount(detectLogicalCpuCount())
{
}

ResourceSnapshot ResourceMonitor::sample()
{
    ResourceSnapshot snapshot;

    bool processCpuOk = false;
    const double processCpuSeconds = readProcessCpuSeconds(&processCpuOk);
    const auto now = std::chrono::steady_clock::now();
    if (processCpuOk) {
        if (m_hasProcessCpuSample) {
            const double elapsedSeconds =
                std::chrono::duration<double>(now - m_lastProcessSampleTime).count();
            const double cpuDeltaSeconds = processCpuSeconds - m_lastProcessCpuSeconds;
            if (elapsedSeconds > 0.0 && cpuDeltaSeconds >= 0.0 && m_logicalCpuCount > 0) {
                const double percent =
                    (cpuDeltaSeconds / elapsedSeconds) * 100.0 / m_logicalCpuCount;
                snapshot.processCpuPercent = std::clamp(percent, 0.0, 100.0);
                snapshot.processCpuValid = true;
            }
        }

        m_lastProcessCpuSeconds = processCpuSeconds;
        m_lastProcessSampleTime = now;
        m_hasProcessCpuSample = true;
    }

    bool processMemoryOk = false;
    const quint64 processMemoryBytes = readProcessMemoryBytes(&processMemoryOk);
    if (processMemoryOk) {
        snapshot.processMemoryBytes = processMemoryBytes;
        snapshot.processMemoryValid = true;
    }

    quint64 systemIdleTicks = 0;
    quint64 systemTotalTicks = 0;
    if (readSystemCpuTicks(&systemIdleTicks, &systemTotalTicks)) {
        if (m_hasSystemCpuSample && systemTotalTicks >= m_lastSystemTotalTicks) {
            const quint64 totalDelta = systemTotalTicks - m_lastSystemTotalTicks;
            const quint64 idleDelta = systemIdleTicks >= m_lastSystemIdleTicks
                ? systemIdleTicks - m_lastSystemIdleTicks
                : 0;
            if (totalDelta > 0) {
                const quint64 busyDelta = totalDelta - std::min(idleDelta, totalDelta);
                snapshot.systemCpuPercent =
                    std::clamp(static_cast<double>(busyDelta) * 100.0 / totalDelta, 0.0, 100.0);
                snapshot.systemCpuValid = true;
            }
        }

        m_lastSystemIdleTicks = systemIdleTicks;
        m_lastSystemTotalTicks = systemTotalTicks;
        m_hasSystemCpuSample = true;
    }

    quint64 systemMemoryUsedBytes = 0;
    quint64 systemMemoryTotalBytes = 0;
    if (readSystemMemory(&systemMemoryUsedBytes, &systemMemoryTotalBytes)) {
        snapshot.systemMemoryUsedBytes = systemMemoryUsedBytes;
        snapshot.systemMemoryTotalBytes = systemMemoryTotalBytes;
        snapshot.systemMemoryValid = true;
    }

    return snapshot;
}

int ResourceMonitor::detectLogicalCpuCount()
{
    return std::max(1, QThread::idealThreadCount());
}

double ResourceMonitor::readProcessCpuSeconds(bool *ok)
{
    if (ok) *ok = false;

#if defined(_WIN32)
    FILETIME createTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        return 0.0;
    }

    if (ok) *ok = true;
    return static_cast<double>(fileTimeToUInt64(kernelTime) + fileTimeToUInt64(userTime))
        / 10000000.0;
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }

    if (ok) *ok = true;
    return timevalToSeconds(usage.ru_utime.tv_sec, usage.ru_utime.tv_usec)
        + timevalToSeconds(usage.ru_stime.tv_sec, usage.ru_stime.tv_usec);
#else
    return 0.0;
#endif
}

quint64 ResourceMonitor::readProcessMemoryBytes(bool *ok)
{
    if (ok) *ok = false;

#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return 0;
    }

    if (ok) *ok = true;
    return static_cast<quint64>(counters.WorkingSetSize);
#elif defined(__APPLE__) && defined(__MACH__)
    task_vm_info_data_t vmInfo{};
    mach_msg_type_number_t vmInfoCount = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(),
                  TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&vmInfo),
                  &vmInfoCount) == KERN_SUCCESS) {
        if (ok) *ok = true;
        return static_cast<quint64>(vmInfo.phys_footprint);
    }

    mach_task_basic_info_data_t basicInfo{};
    mach_msg_type_number_t basicInfoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(),
                  MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&basicInfo),
                  &basicInfoCount) == KERN_SUCCESS) {
        if (ok) *ok = true;
        return static_cast<quint64>(basicInfo.resident_size);
    }

    return 0;
#elif defined(__linux__)
    QFile file(QStringLiteral("/proc/self/statm"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QList<QByteArray> parts = file.readAll().simplified().split(' ');
    if (parts.size() < 2) {
        return 0;
    }

    bool parsed = false;
    const quint64 rssPages = parts.at(1).toULongLong(&parsed);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (!parsed || pageSize <= 0) {
        return 0;
    }

    if (ok) *ok = true;
    return rssPages * static_cast<quint64>(pageSize);
#else
    return 0;
#endif
}

bool ResourceMonitor::readSystemCpuTicks(quint64 *idleTicks, quint64 *totalTicks)
{
    if (!idleTicks || !totalTicks) {
        return false;
    }

#if defined(_WIN32)
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return false;
    }

    *idleTicks = fileTimeToUInt64(idleTime);
    *totalTicks = fileTimeToUInt64(kernelTime) + fileTimeToUInt64(userTime);
    return true;
#elif defined(__APPLE__) && defined(__MACH__)
    natural_t processorCount = 0;
    processor_cpu_load_info_t processorInfo = nullptr;
    mach_msg_type_number_t processorInfoCount = 0;

    const kern_return_t result = host_processor_info(mach_host_self(),
                                                     PROCESSOR_CPU_LOAD_INFO,
                                                     &processorCount,
                                                     reinterpret_cast<processor_info_array_t *>(&processorInfo),
                                                     &processorInfoCount);
    if (result != KERN_SUCCESS || processorInfo == nullptr) {
        return false;
    }

    quint64 idle = 0;
    quint64 total = 0;
    for (natural_t i = 0; i < processorCount; ++i) {
        const quint64 user = processorInfo[i].cpu_ticks[CPU_STATE_USER];
        const quint64 system = processorInfo[i].cpu_ticks[CPU_STATE_SYSTEM];
        const quint64 nice = processorInfo[i].cpu_ticks[CPU_STATE_NICE];
        const quint64 idleState = processorInfo[i].cpu_ticks[CPU_STATE_IDLE];
        idle += idleState;
        total += user + system + nice + idleState;
    }

    vm_deallocate(mach_task_self(),
                  reinterpret_cast<vm_address_t>(processorInfo),
                  static_cast<vm_size_t>(processorInfoCount * sizeof(integer_t)));

    *idleTicks = idle;
    *totalTicks = total;
    return true;
#elif defined(__linux__)
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QList<QByteArray> parts = file.readLine().simplified().split(' ');
    if (parts.size() < 5 || parts.at(0) != "cpu") {
        return false;
    }

    quint64 total = 0;
    quint64 idle = 0;
    for (int i = 1; i < parts.size(); ++i) {
        bool parsed = false;
        const quint64 value = parts.at(i).toULongLong(&parsed);
        if (!parsed) {
            return false;
        }

        total += value;
        if (i == 4 || i == 5) {
            idle += value;
        }
    }

    *idleTicks = idle;
    *totalTicks = total;
    return true;
#else
    return false;
#endif
}

bool ResourceMonitor::readSystemMemory(quint64 *usedBytes, quint64 *totalBytes)
{
    if (!usedBytes || !totalBytes) {
        return false;
    }

#if defined(_WIN32)
    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof(memoryStatus);
    if (!GlobalMemoryStatusEx(&memoryStatus)) {
        return false;
    }

    *totalBytes = static_cast<quint64>(memoryStatus.ullTotalPhys);
    *usedBytes = static_cast<quint64>(memoryStatus.ullTotalPhys - memoryStatus.ullAvailPhys);
    return *totalBytes > 0;
#elif defined(__APPLE__) && defined(__MACH__)
    quint64 total = 0;
    size_t totalSize = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &totalSize, nullptr, 0) != 0 || total == 0) {
        return false;
    }

    vm_size_t pageSize = 0;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS || pageSize == 0) {
        return false;
    }

    vm_statistics64_data_t stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(),
                          HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&stats),
                          &count) != KERN_SUCCESS) {
        return false;
    }

    const quint64 availablePages =
        static_cast<quint64>(stats.free_count) + static_cast<quint64>(stats.speculative_count);
    const quint64 availableBytes = availablePages * static_cast<quint64>(pageSize);
    *totalBytes = total;
    *usedBytes = total > availableBytes ? total - availableBytes : 0;
    return true;
#elif defined(__linux__)
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    quint64 total = 0;
    quint64 available = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() < 2) {
            continue;
        }

        bool parsed = false;
        const quint64 valueBytes = parts.at(1).toULongLong(&parsed) * 1024;
        if (!parsed) {
            continue;
        }

        if (parts.at(0) == "MemTotal:") {
            total = valueBytes;
        } else if (parts.at(0) == "MemAvailable:") {
            available = valueBytes;
        }
    }

    if (total == 0 || available > total) {
        return false;
    }

    *totalBytes = total;
    *usedBytes = total - available;
    return true;
#else
    return false;
#endif
}
