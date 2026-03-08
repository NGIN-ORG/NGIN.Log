#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Sink.hpp>

#include <cstdio>
#include <mutex>
#include <string>

namespace NGIN::Log
{
    /// @brief File sink configuration.
    struct FileSinkOptions
    {
        bool append {true};
        bool autoFlush {false};
    };

    /// @brief Thread-safe file sink.
    class NGIN_LOG_API FileSink final : public ILogSink
    {
    public:
        /// @brief Construct sink for a file path.
        explicit FileSink(std::string path, FileSinkOptions options = {}) noexcept;

        /// @brief Flush and close file.
        ~FileSink() override;

        FileSink(const FileSink&) = delete;
        auto operator=(const FileSink&) -> FileSink& = delete;

        /// @brief Whether file handle is currently open.
        [[nodiscard]] auto IsOpen() const noexcept -> bool;

        /// @brief Reopen current path using configured mode.
        [[nodiscard]] auto Reopen() noexcept -> bool;

        /// @brief Write a single record to file.
        void Write(const LogRecordView& record) noexcept override;

        /// @brief Flush file stream.
        void Flush() noexcept override;

    private:
        [[nodiscard]] auto OpenFileLocked() noexcept -> bool;

        std::string        m_path;
        FileSinkOptions    m_options {};
        std::FILE*         m_file {nullptr};
        mutable std::mutex m_mutex {};
    };
}
