#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Sink.hpp>

#include <mutex>

namespace NGIN::Log
{
    /// @brief Console sink configuration.
    struct ConsoleSinkOptions
    {
        bool useStderrForErrors {true};
        bool includeSource {true};
        bool autoFlush {false};
    };

    /// @brief Thread-safe console sink.
    class NGIN_LOG_API ConsoleSink final : public ILogSink
    {
    public:
        /// @brief Construct console sink with options.
        explicit ConsoleSink(ConsoleSinkOptions options = {}) noexcept;

        /// @brief Write record to stdout/stderr.
        void Write(const LogRecordView& record) noexcept override;

        /// @brief Flush stdout/stderr.
        void Flush() noexcept override;

    private:
        ConsoleSinkOptions m_options {};
        std::mutex         m_mutex {};
    };
}
