#include <NGIN/Log/Sinks/FileSink.hpp>

#include "RecordLine.hpp"

namespace NGIN::Log
{
    FileSink::FileSink(std::string path, const FileSinkOptions options) noexcept
        : m_path(std::move(path))
        , m_options(options)
    {
        std::lock_guard lock(m_mutex);
        (void)OpenFileLocked();
    }

    FileSink::~FileSink()
    {
        std::lock_guard lock(m_mutex);
        if (m_file)
        {
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }
    }

    auto FileSink::IsOpen() const noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        return m_file != nullptr;
    }

    auto FileSink::Reopen() noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        if (m_file)
        {
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }
        return OpenFileLocked();
    }

    void FileSink::Write(const LogRecordView& record) noexcept
    {
        const auto line = detail::FormatRecordLine(record, true);

        std::lock_guard lock(m_mutex);
        if (!m_file && !OpenFileLocked())
        {
            return;
        }

        std::fwrite(line.data(), 1, line.size(), m_file);
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
}
