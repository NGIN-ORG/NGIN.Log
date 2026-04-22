#pragma once

#include <NGIN/Log/RecordFormatter.hpp>
#include <NGIN/Log/Sink.hpp>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace NGIN::Log
{
    struct FileSinkOptions
    {
        bool               append {true};
        bool               autoFlush {false};
        RecordFormatterPtr formatter {};
    };

    class NGIN_LOG_API FileSink : public ILogSink
    {
    public:
        explicit FileSink(std::string path, FileSinkOptions options = {}) noexcept;
        ~FileSink() override;

        FileSink(const FileSink&) = delete;
        auto operator=(const FileSink&) -> FileSink& = delete;

        [[nodiscard]] auto IsOpen() const noexcept -> bool;
        [[nodiscard]] auto Reopen() noexcept -> bool;

        void Write(const LogRecordView& record) noexcept override;
        void Flush() noexcept override;

        [[nodiscard]] static auto CreateDefaultFormatter() noexcept -> RecordFormatterPtr;

    protected:
        struct FileIdentity
        {
            bool        valid {false};
            std::uint64_t device {0};
            std::uint64_t inode {0};
        };

        [[nodiscard]] auto OpenFileLocked() noexcept -> bool;
        void CloseFileLocked() noexcept;
        void FormatRecordLocked(const LogRecordView& record) noexcept;

        std::string        m_path;
        FileSinkOptions    m_options {};
        std::FILE*         m_file {nullptr};
        mutable std::mutex m_mutex {};
        RecordFormatterPtr m_formatter {};
        std::string        m_scratch {};
    };
}
