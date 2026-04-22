#pragma once

#include <NGIN/Log/Sinks/FileSink.hpp>

#include <cstdint>

namespace NGIN::Log
{
    struct RotatingFileSinkOptions
    {
        std::string        path {};
        bool               append {true};
        bool               autoFlush {false};
        std::uint64_t      maxFileBytes {0};
        std::size_t        maxFiles {5};
        bool               rotateAtStartup {false};
        bool               rotateDailyLocal {false};
        bool               detectExternalRotation {false};
        RecordFormatterPtr formatter {};
    };

    class NGIN_LOG_API RotatingFileSink final : public ILogSink
    {
    public:
        struct FileIdentity
        {
            bool          valid {false};
            std::uint64_t device {0};
            std::uint64_t inode {0};
        };

        explicit RotatingFileSink(RotatingFileSinkOptions options) noexcept;
        ~RotatingFileSink() override;

        RotatingFileSink(const RotatingFileSink&) = delete;
        auto operator=(const RotatingFileSink&) -> RotatingFileSink& = delete;

        [[nodiscard]] auto IsOpen() const noexcept -> bool;
        [[nodiscard]] auto Reopen() noexcept -> bool;
        [[nodiscard]] auto ForceRotate() noexcept -> bool;

        void Write(const LogRecordView& record) noexcept override;
        void Flush() noexcept override;

    private:
        [[nodiscard]] auto OpenFileLocked() noexcept -> bool;
        void CloseFileLocked() noexcept;
        [[nodiscard]] auto RotateLocked() noexcept -> bool;
        [[nodiscard]] auto ShouldRotateByDateLocked(const LogRecordView& record) noexcept -> bool;
        [[nodiscard]] auto DetectExternalRotationLocked() noexcept -> bool;

        RotatingFileSinkOptions m_options {};
        RecordFormatterPtr      m_formatter {};
        std::FILE*              m_file {nullptr};
        mutable std::mutex      m_mutex {};
        std::string             m_scratch {};
        std::uint64_t           m_currentFileBytes {0};
        std::int64_t            m_lastLocalDateKey {-1};
        FileIdentity            m_identity {};
    };
}
