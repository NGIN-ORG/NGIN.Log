#include <NGIN/Log/Sinks/ConsoleSink.hpp>

#include "RecordLine.hpp"

#include <NGIN/Log/LogLevel.hpp>

#include <cstdio>

namespace NGIN::Log
{
    ConsoleSink::ConsoleSink(const ConsoleSinkOptions options) noexcept
        : m_options(options)
    {
    }

    void ConsoleSink::Write(const LogRecordView& record) noexcept
    {
        const auto line = detail::FormatRecordLine(record, m_options.includeSource);

        std::lock_guard lock(m_mutex);
        std::FILE*      stream = stdout;
        if (m_options.useStderrForErrors &&
            (record.level == LogLevel::Error || record.level == LogLevel::Fatal || record.level == LogLevel::Warn))
        {
            stream = stderr;
        }

        std::fwrite(line.data(), 1, line.size(), stream);

        if (m_options.autoFlush)
        {
            std::fflush(stream);
        }
    }

    void ConsoleSink::Flush() noexcept
    {
        std::lock_guard lock(m_mutex);
        std::fflush(stdout);
        std::fflush(stderr);
    }
}
