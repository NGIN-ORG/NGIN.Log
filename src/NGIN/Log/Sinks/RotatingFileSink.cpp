#include <NGIN/Log/Sinks/RotatingFileSink.hpp>

#include <filesystem>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace NGIN::Log
{
    namespace
    {
        [[nodiscard]] auto MakeLocalDateKey(const NGIN::UInt64 timestampEpochNanoseconds) noexcept -> std::int64_t
        {
            const auto seconds = static_cast<std::time_t>(timestampEpochNanoseconds / 1000000000ULL);
            std::tm    timeInfo {};
#if defined(_WIN32)
            localtime_s(&timeInfo, &seconds);
#else
            localtime_r(&seconds, &timeInfo);
#endif
            return static_cast<std::int64_t>((timeInfo.tm_year + 1900) * 1000 + timeInfo.tm_yday);
        }

        [[nodiscard]] auto ReadFileIdentity(std::FILE* file) noexcept -> RotatingFileSink::FileIdentity
        {
            RotatingFileSink::FileIdentity identity {};
#if defined(__unix__) || defined(__APPLE__)
            if (file == nullptr)
            {
                return identity;
            }

            struct stat status {};
            if (fstat(fileno(file), &status) == 0)
            {
                identity.valid = true;
                identity.device = static_cast<std::uint64_t>(status.st_dev);
                identity.inode = static_cast<std::uint64_t>(status.st_ino);
            }
#endif
            return identity;
        }

        [[nodiscard]] auto ReadPathIdentity(const std::string& path) noexcept -> RotatingFileSink::FileIdentity
        {
            RotatingFileSink::FileIdentity identity {};
#if defined(__unix__) || defined(__APPLE__)
            struct stat status {};
            if (stat(path.c_str(), &status) == 0)
            {
                identity.valid = true;
                identity.device = static_cast<std::uint64_t>(status.st_dev);
                identity.inode = static_cast<std::uint64_t>(status.st_ino);
            }
#else
            (void)path;
#endif
            return identity;
        }
    }

    RotatingFileSink::RotatingFileSink(RotatingFileSinkOptions options) noexcept
        : m_options(std::move(options))
        , m_formatter(m_options.formatter ? m_options.formatter : FileSink::CreateDefaultFormatter())
    {
        m_scratch.reserve(256 + Config::MaxMessageBytes);
        std::lock_guard lock(m_mutex);
        (void)OpenFileLocked();
        if (m_options.rotateAtStartup)
        {
            (void)RotateLocked();
        }
    }

    RotatingFileSink::~RotatingFileSink()
    {
        std::lock_guard lock(m_mutex);
        CloseFileLocked();
    }

    auto RotatingFileSink::IsOpen() const noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        return m_file != nullptr;
    }

    auto RotatingFileSink::Reopen() noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        CloseFileLocked();
        return OpenFileLocked();
    }

    auto RotatingFileSink::ForceRotate() noexcept -> bool
    {
        std::lock_guard lock(m_mutex);
        return RotateLocked();
    }

    void RotatingFileSink::Write(const LogRecordView& record) noexcept
    {
        std::lock_guard lock(m_mutex);

        if (!m_formatter)
        {
            m_formatter = FileSink::CreateDefaultFormatter();
        }
        if (!m_formatter)
        {
            return;
        }

        m_formatter->Format(record, m_scratch);

        if (!m_file && !OpenFileLocked())
        {
            return;
        }

        if (m_options.detectExternalRotation)
        {
            (void)DetectExternalRotationLocked();
        }

        if ((m_options.maxFileBytes > 0 && m_currentFileBytes + m_scratch.size() > m_options.maxFileBytes) ||
            ShouldRotateByDateLocked(record))
        {
            (void)RotateLocked();
        }

        if (!m_file && !OpenFileLocked())
        {
            return;
        }

        std::fwrite(m_scratch.data(), 1, m_scratch.size(), m_file);
        m_currentFileBytes += static_cast<std::uint64_t>(m_scratch.size());
        if (m_options.autoFlush)
        {
            std::fflush(m_file);
        }
    }

    void RotatingFileSink::Flush() noexcept
    {
        std::lock_guard lock(m_mutex);
        if (m_file)
        {
            std::fflush(m_file);
        }
    }

    auto RotatingFileSink::OpenFileLocked() noexcept -> bool
    {
        const char* mode = m_options.append ? "ab" : "wb";
        m_file = std::fopen(m_options.path.c_str(), mode);
        if (!m_file)
        {
            return false;
        }

        std::error_code ec;
        m_currentFileBytes = std::filesystem::exists(m_options.path, ec) ? std::filesystem::file_size(m_options.path, ec) : 0;
        if (ec)
        {
            m_currentFileBytes = 0;
        }

        m_identity = ReadFileIdentity(m_file);
        return true;
    }

    void RotatingFileSink::CloseFileLocked() noexcept
    {
        if (m_file)
        {
            std::fflush(m_file);
            std::fclose(m_file);
            m_file = nullptr;
        }
        m_identity = {};
    }

    auto RotatingFileSink::RotateLocked() noexcept -> bool
    {
        CloseFileLocked();

        std::error_code ec;
        if (m_options.maxFiles > 0)
        {
            const auto oldest = m_options.path + "." + std::to_string(m_options.maxFiles);
            std::filesystem::remove(oldest, ec);
            ec.clear();

            for (std::size_t index = m_options.maxFiles; index > 1; --index)
            {
                const auto from = m_options.path + "." + std::to_string(index - 1);
                const auto to = m_options.path + "." + std::to_string(index);
                if (std::filesystem::exists(from, ec))
                {
                    std::filesystem::rename(from, to, ec);
                    ec.clear();
                }
            }

            if (std::filesystem::exists(m_options.path, ec))
            {
                std::filesystem::rename(m_options.path, m_options.path + ".1", ec);
            }
        }
        else
        {
            std::filesystem::remove(m_options.path, ec);
        }

        m_currentFileBytes = 0;
        return OpenFileLocked();
    }

    auto RotatingFileSink::ShouldRotateByDateLocked(const LogRecordView& record) noexcept -> bool
    {
        if (!m_options.rotateDailyLocal)
        {
            return false;
        }

        const auto dateKey = MakeLocalDateKey(record.timestampEpochNanoseconds);
        if (m_lastLocalDateKey == -1)
        {
            m_lastLocalDateKey = dateKey;
            return false;
        }

        if (dateKey != m_lastLocalDateKey)
        {
            m_lastLocalDateKey = dateKey;
            return true;
        }

        return false;
    }

    auto RotatingFileSink::DetectExternalRotationLocked() noexcept -> bool
    {
        const auto pathIdentity = ReadPathIdentity(m_options.path);
        if (!m_identity.valid || !pathIdentity.valid)
        {
            return false;
        }

        if (m_identity.device == pathIdentity.device && m_identity.inode == pathIdentity.inode)
        {
            return false;
        }

        CloseFileLocked();
        return OpenFileLocked();
    }
}
