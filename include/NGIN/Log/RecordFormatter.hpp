#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Types.hpp>

#include <concepts>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace NGIN::Log
{
    enum class TimestampStyle : NGIN::UInt8
    {
        EpochNanoseconds,
        EpochMilliseconds,
        Iso8601Utc,
        Iso8601Local,
    };

    class NGIN_LOG_API IRecordFormatter
    {
    public:
        virtual ~IRecordFormatter() = default;

        virtual void Format(const LogRecordView& record, std::string& output) noexcept = 0;
    };

    using RecordFormatterPtr = std::shared_ptr<IRecordFormatter>;

    template<class TFormatter, class... Args>
        requires std::derived_from<TFormatter, IRecordFormatter>
    [[nodiscard]] auto MakeRecordFormatter(Args&&... args) noexcept -> RecordFormatterPtr
    {
        try
        {
            return std::make_shared<TFormatter>(std::forward<Args>(args)...);
        }
        catch (...)
        {
            return {};
        }
    }

    struct TextRecordFormatterOptions
    {
        bool           includeSource {true};
        TimestampStyle timestampStyle {TimestampStyle::Iso8601Local};
    };

    struct JsonRecordFormatterOptions
    {
        TimestampStyle timestampStyle {TimestampStyle::EpochNanoseconds};
    };

    struct LogFmtRecordFormatterOptions
    {
        bool           includeSource {true};
        TimestampStyle timestampStyle {TimestampStyle::EpochMilliseconds};
    };

    class NGIN_LOG_API TextRecordFormatter final : public IRecordFormatter
    {
    public:
        explicit TextRecordFormatter(TextRecordFormatterOptions options = {}) noexcept;

        void Format(const LogRecordView& record, std::string& output) noexcept override;

    private:
        struct TimestampCache
        {
            NGIN::Int64 second {-1};
            std::string prefix {};
            std::string suffix {};
        };

        TextRecordFormatterOptions m_options {};
        mutable std::mutex         m_cacheMutex {};
        mutable TimestampCache     m_cache {};
    };

    class NGIN_LOG_API JsonRecordFormatter final : public IRecordFormatter
    {
    public:
        explicit JsonRecordFormatter(JsonRecordFormatterOptions options = {}) noexcept;

        void Format(const LogRecordView& record, std::string& output) noexcept override;

    private:
        JsonRecordFormatterOptions m_options {};
    };

    class NGIN_LOG_API LogFmtRecordFormatter final : public IRecordFormatter
    {
    public:
        explicit LogFmtRecordFormatter(LogFmtRecordFormatterOptions options = {}) noexcept;

        void Format(const LogRecordView& record, std::string& output) noexcept override;

    private:
        LogFmtRecordFormatterOptions m_options {};
    };
}
