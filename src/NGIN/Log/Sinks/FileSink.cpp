#include <NGIN/Log/Sinks/FileSink.hpp>

namespace NGIN::Log
{
    FileSink::FileSink(std::string path, const FileSinkOptions options) noexcept
        : m_path(std::move(path))
        , m_options(options)
        , m_formatter(options.formatter ? options.formatter : CreateDefaultFormatter())
    {
        m_scratch.reserve(256 + Config::MaxMessageBytes);
        std::lock_guard lock(m_mutex);
        (void)OpenFileLocked();
    }

    FileSink::~FileSink()
    {
        std::lock_guard lock(m_mutex);
        CloseFileLocked();
    }

    auto FileSink::CreateDefaultFormatter() noexcept -> RecordFormatterPtr
    {
        return MakeRecordFormatter<TextRecordFormatter>(TextRecordFormatterOptions {
            .includeSource = true,
            .timestampStyle = TimestampStyle::Iso8601Local,
        });
    }

    auto FileSink::IsOpen() const noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        return m_file != nullptr;
    }

    auto FileSink::Reopen() noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        CloseFileLocked();
        return OpenFileLocked();
    }

    void FileSink::FormatRecordLocked(const LogRecordView& record) noexcept
    {
        if (!m_formatter)
        {
            m_formatter = CreateDefaultFormatter();
        }

        if (m_formatter)
        {
            m_formatter->Format(record, m_scratch);
        }
        else
        {
            m_scratch.clear();
        }
    }

    void FileSink::Write(const LogRecordView& record) noexcept
    {
        std::lock_guard lock(m_mutex);
        FormatRecordLocked(record);

        if (!m_file && !OpenFileLocked())
        {
            return;
        }

        std::fwrite(m_scratch.data(), 1, m_scratch.size(), m_file);
        if (m_options.autoFlush)
        {
            std::fflush(m_file);
        }
    }

    void FileSink::Flush() noexcept
    {
        std::lock_guard lock(m_mutex);
        if (m_file)
        {
            std::fflush(m_file);
        }
    }

    auto FileSink::OpenFileLocked() noexcept -> bool
    {
        const char* mode = m_options.append ? "ab" : "wb";
        m_file = std::fopen(m_path.c_str(), mode);
        return m_file != nullptr;
    }

    void FileSink::CloseFileLocked() noexcept
    {
        if (m_file)
        {
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }
    }
}
