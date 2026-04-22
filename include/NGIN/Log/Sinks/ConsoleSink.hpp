#pragma once

#include <NGIN/Log/RecordFormatter.hpp>
#include <NGIN/Log/Sink.hpp>

#include <mutex>
#include <string>

namespace NGIN::Log
{
    struct ConsoleSinkOptions
    {
        bool               useStderrForErrors {true};
        bool               includeSource {true};
        bool               autoFlush {false};
        RecordFormatterPtr formatter {};
    };

    class NGIN_LOG_API ConsoleSink final : public ILogSink
    {
    public:
        explicit ConsoleSink(ConsoleSinkOptions options = {}) noexcept;

        void Write(const LogRecordView& record) noexcept override;
        void Flush() noexcept override;

    private:
        [[nodiscard]] static auto CreateDefaultFormatter(const ConsoleSinkOptions& options) noexcept -> RecordFormatterPtr;

        ConsoleSinkOptions m_options {};
        RecordFormatterPtr m_formatter {};
        std::mutex         m_mutex {};
        std::string        m_scratch {};
    };
}
