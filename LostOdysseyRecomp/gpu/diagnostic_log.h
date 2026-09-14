#pragma once

#include <plume_log.h>
#include <os/logger.h>
#include <os/log_file.h>
#include <string_view>
#include <cstdio>

namespace gpu::diagnostics
{
inline void PlumeLog(const plume::LogRecord& record) noexcept
{
    try {
        std::string_view detail = record.detail ? record.detail : "";
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r')) detail.remove_suffix(1);
        const auto severity = record.severity == plume::LogSeverity::Warning ? LogType::Warning : LogType::Error;
        os::logger::Log(severity, nullptr, "gpu API: backend={} api={} domain={} code={:#010x} occurrence={} {}",
            record.backend ? record.backend : "unknown", record.api ? record.api : "unknown",
            plume::LogErrorDomainName(record.domain), record.rawCode, record.occurrence, detail);
    } catch (...) {
        // Allocation failure is exactly when heap-backed formatting can fail.
        // Keep the original API/code using bounded stack storage in that case.
        char text[1536];
        const int size = std::snprintf(text, sizeof(text),
            "[error] gpu API: backend=%s api=%s domain=%s code=0x%08X occurrence=%u %s\n",
            record.backend ? record.backend : "unknown", record.api ? record.api : "unknown",
            plume::LogErrorDomainName(record.domain), unsigned(record.rawCode), unsigned(record.occurrence),
            record.detail ? record.detail : "");
        if (size > 0) os::logger::EmergencyWrite(text, size_t(size) < sizeof(text) ? size_t(size) : sizeof(text) - 1);
    }
}
inline void InstallPlumeLog() noexcept { plume::SetLogCallback(PlumeLog); }
}
