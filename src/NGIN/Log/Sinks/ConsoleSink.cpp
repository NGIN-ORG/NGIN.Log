#include <NGIN/Log/Sinks/ConsoleSink.hpp>

#include <NGIN/Log/RecordFormatter.hpp>

#include <cstdio>

namespace NGIN::Log
{
    ConsoleSink::ConsoleSink(const ConsoleSinkOptions options) noexcept
        : m_options(options)
        , m_formatter(options.formatter ? options.formatter : CreateDefaultFormatter(options))
    {
        m_scratch.reserve(256 + Config::MaxMessageBytes);
    }

    auto ConsoleSink::CreateDefaultFormatter(const ConsoleSinkOptions& options) noexcept -> RecordFormatterPtr
    {
        return MakeRecordFormatter<TextRecordFormatter>(TextRecordFormatterOptions {
            .includeSource = options.includeSource,
            .timestampStyle = TimestampStyle::Iso8601Local,
        });
    }

    void ConsoleSink::Write(const LogRecordView& record) noexcept
    {
        std::lock_guard lock(m_mutex);

        if (!m_formatter)
        {
            m_formatter = CreateDefaultFormatter(m_options);
        }

        if (!m_formatter)
        {
            return;
        }

        m_formatter->Format(record, m_scratch);

        std::FILE* stream = stdout;
        if (m_options.useStderrForErrors &&
            (record.level == LogLevel::Warn || record.level == LogLevel::Error || record.level == LogLevel::Fatal))
        {
            stream = stderr;
        }

        std::fwrite(m_scratch.data(), 1, m_scratch.size(), stream);
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
