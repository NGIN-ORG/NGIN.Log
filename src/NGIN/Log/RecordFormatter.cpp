#include <NGIN/Log/RecordFormatter.hpp>

#include "FormattingUtilities.hpp"

namespace NGIN::Log
{
    TextRecordFormatter::TextRecordFormatter(TextRecordFormatterOptions options) noexcept
        : m_options(options)
    {
    }

    void TextRecordFormatter::Format(const LogRecordView& record, std::string& output) noexcept
    {
        output.clear();
        output.reserve(256 + record.message.size());

        output.push_back('[');
        const auto second = static_cast<NGIN::Int64>(record.timestampEpochNanoseconds / 1000000000ULL);
        {
            std::lock_guard lock(m_cacheMutex);
            if ((m_options.timestampStyle == TimestampStyle::Iso8601Utc ||
                 m_options.timestampStyle == TimestampStyle::Iso8601Local) &&
                m_cache.second != second)
            {
                detail::FillCachedTimeParts(m_options.timestampStyle, second, m_cache.prefix, m_cache.suffix);
                m_cache.second = second;
            }

            detail::AppendTimestamp(
                output,
                record.timestampEpochNanoseconds,
                m_options.timestampStyle,
                m_cache.prefix,
                m_cache.suffix);
        }
        output += "][";
        detail::AppendString(output, ToString(record.level));
        output += "][";
        detail::AppendEscapedText(output, record.loggerName);
        output += "] ";
        detail::AppendEscapedText(output, record.message);

        if (!record.attributes.empty())
        {
            output += " {";
            for (std::size_t i = 0; i < record.attributes.size(); ++i)
            {
                if (i > 0)
                {
                    output += ", ";
                }

                detail::AppendEscapedText(output, record.attributes[i].key);
                output.push_back('=');
                detail::AppendAttributeValue(output, record.attributes[i].value);
            }
            output.push_back('}');
        }

        if (record.truncatedAttributeCount > 0 || record.truncatedBytes > 0)
        {
            output += " [truncated attrs=";
            detail::AppendUnsigned(output, record.truncatedAttributeCount);
            output += ", bytes=";
            detail::AppendUnsigned(output, record.truncatedBytes);
            output.push_back(']');
        }

        if (m_options.includeSource)
        {
            output += " (";
            detail::AppendEscapedText(output, record.source.file_name());
            output.push_back(':');
            detail::AppendUnsigned(output, record.source.line());
            output.push_back(')');
        }

        output.push_back('\n');
    }

    JsonRecordFormatter::JsonRecordFormatter(JsonRecordFormatterOptions options) noexcept
        : m_options(options)
    {
    }

    void JsonRecordFormatter::Format(const LogRecordView& record, std::string& output) noexcept
    {
        output.clear();
        output.reserve(256 + record.message.size());

        output += "{\"timestamp\":";
        detail::AppendTimestamp(output, record.timestampEpochNanoseconds, m_options.timestampStyle);
        output += ",\"level\":\"";
        detail::AppendJsonEscapedText(output, ToString(record.level));
        output += "\",\"logger\":\"";
        detail::AppendJsonEscapedText(output, record.loggerName);
        output += "\",\"message\":\"";
        detail::AppendJsonEscapedText(output, record.message);
        output += "\",\"source\":\"";
        detail::AppendJsonEscapedText(output, record.source.file_name());
        output.push_back(':');
        detail::AppendUnsigned(output, record.source.line());
        output += "\",\"thread_id\":";
        detail::AppendUnsigned(output, record.threadIdHash);
        output += ",\"attributes\":{";

        for (std::size_t i = 0; i < record.attributes.size(); ++i)
        {
            if (i > 0)
            {
                output.push_back(',');
            }

            output.push_back('"');
            detail::AppendJsonEscapedText(output, record.attributes[i].key);
            output += "\":";
            detail::AppendJsonAttributeValue(output, record.attributes[i].value);
        }

        output += "},\"truncated_attributes\":";
        detail::AppendUnsigned(output, record.truncatedAttributeCount);
        output += ",\"truncated_bytes\":";
        detail::AppendUnsigned(output, record.truncatedBytes);
        output += "}\n";
    }

    LogFmtRecordFormatter::LogFmtRecordFormatter(LogFmtRecordFormatterOptions options) noexcept
        : m_options(options)
    {
    }

    void LogFmtRecordFormatter::Format(const LogRecordView& record, std::string& output) noexcept
    {
        output.clear();
        output.reserve(256 + record.message.size());

        output += "ts=";
        detail::AppendTimestamp(output, record.timestampEpochNanoseconds, m_options.timestampStyle);
        output += " level=";
        detail::AppendEscapedText(output, ToString(record.level));
        output += " logger=\"";
        detail::AppendEscapedText(output, record.loggerName);
        output += "\" msg=\"";
        detail::AppendEscapedText(output, record.message);
        output.push_back('"');
        output += " thread_id=";
        detail::AppendUnsigned(output, record.threadIdHash);

        if (m_options.includeSource)
        {
            output += " source=\"";
            detail::AppendEscapedText(output, record.source.file_name());
            output.push_back(':');
            detail::AppendUnsigned(output, record.source.line());
            output.push_back('"');
        }

        for (const auto& attribute : record.attributes)
        {
            output.push_back(' ');
            detail::AppendEscapedText(output, attribute.key);
            output.push_back('=');
            detail::AppendLogFmtAttributeValue(output, attribute.value);
        }

        if (record.truncatedAttributeCount > 0 || record.truncatedBytes > 0)
        {
            output += " truncated_attrs=";
            detail::AppendUnsigned(output, record.truncatedAttributeCount);
            output += " truncated_bytes=";
            detail::AppendUnsigned(output, record.truncatedBytes);
        }

        output.push_back('\n');
    }
}
