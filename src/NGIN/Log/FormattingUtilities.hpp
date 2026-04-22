#pragma once

#include <NGIN/Log/RecordFormatter.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <string>
#include <string_view>

namespace NGIN::Log::detail
{
    inline void AppendString(std::string& output, const std::string_view value)
    {
        output.append(value.data(), value.size());
    }

    inline void AppendUnsigned(std::string& output, const NGIN::UInt64 value)
    {
        std::array<char, 32> buffer {};
        const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (ec == std::errc {})
        {
            output.append(buffer.data(), static_cast<std::size_t>(ptr - buffer.data()));
        }
    }

    inline void AppendSigned(std::string& output, const NGIN::Int64 value)
    {
        std::array<char, 32> buffer {};
        const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        if (ec == std::errc {})
        {
            output.append(buffer.data(), static_cast<std::size_t>(ptr - buffer.data()));
        }
    }

    inline void AppendDouble(std::string& output, const double value)
    {
        try
        {
            std::format_to(std::back_inserter(output), "{}", value);
        }
        catch (...)
        {
            output += "0";
        }
    }

    inline void AppendHexByte(std::string& output, const unsigned char value)
    {
        constexpr char Digits[] = "0123456789abcdef";
        output.push_back(Digits[(value >> 4) & 0x0F]);
        output.push_back(Digits[value & 0x0F]);
    }

    inline void AppendEscapedText(std::string& output, const std::string_view text)
    {
        for (const char rawCh : text)
        {
            const auto ch = static_cast<unsigned char>(rawCh);
            switch (ch)
            {
                case '\\': output += "\\\\"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                case '"': output += "\\\""; break;
                default:
                    if (ch < 0x20)
                    {
                        output += "\\x";
                        AppendHexByte(output, ch);
                    }
                    else
                    {
                        output.push_back(static_cast<char>(ch));
                    }
                    break;
            }
        }
    }

    inline void AppendJsonEscapedText(std::string& output, const std::string_view text)
    {
        for (const char rawCh : text)
        {
            const auto ch = static_cast<unsigned char>(rawCh);
            switch (ch)
            {
                case '\\': output += "\\\\"; break;
                case '"': output += "\\\""; break;
                case '\b': output += "\\b"; break;
                case '\f': output += "\\f"; break;
                case '\n': output += "\\n"; break;
                case '\r': output += "\\r"; break;
                case '\t': output += "\\t"; break;
                default:
                    if (ch < 0x20)
                    {
                        output += "\\u00";
                        AppendHexByte(output, ch);
                    }
                    else
                    {
                        output.push_back(static_cast<char>(ch));
                    }
                    break;
            }
        }
    }

    inline void AppendAttributeValue(std::string& output, const AttributeValue& value)
    {
        std::visit(
            [&](const auto& v) {
                using TValue = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<TValue, NGIN::Int64>)
                {
                    AppendSigned(output, v);
                }
                else if constexpr (std::is_same_v<TValue, NGIN::UInt64>)
                {
                    AppendUnsigned(output, v);
                }
                else if constexpr (std::is_same_v<TValue, double>)
                {
                    AppendDouble(output, v);
                }
                else if constexpr (std::is_same_v<TValue, bool>)
                {
                    output += v ? "true" : "false";
                }
                else if constexpr (std::is_same_v<TValue, std::string_view>)
                {
                    output.push_back('"');
                    AppendEscapedText(output, v);
                    output.push_back('"');
                }
            },
            value);
    }

    inline void AppendJsonAttributeValue(std::string& output, const AttributeValue& value)
    {
        std::visit(
            [&](const auto& v) {
                using TValue = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<TValue, NGIN::Int64>)
                {
                    AppendSigned(output, v);
                }
                else if constexpr (std::is_same_v<TValue, NGIN::UInt64>)
                {
                    AppendUnsigned(output, v);
                }
                else if constexpr (std::is_same_v<TValue, double>)
                {
                    AppendDouble(output, v);
                }
                else if constexpr (std::is_same_v<TValue, bool>)
                {
                    output += v ? "true" : "false";
                }
                else if constexpr (std::is_same_v<TValue, std::string_view>)
                {
                    output.push_back('"');
                    AppendJsonEscapedText(output, v);
                    output.push_back('"');
                }
            },
            value);
    }

    inline void AppendLogFmtAttributeValue(std::string& output, const AttributeValue& value)
    {
        std::visit(
            [&](const auto& v) {
                using TValue = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<TValue, std::string_view>)
                {
                    output.push_back('"');
                    AppendEscapedText(output, v);
                    output.push_back('"');
                }
                else
                {
                    AppendJsonAttributeValue(output, value);
                }
            },
            value);
    }

    inline void AppendSource(std::string& output, const std::source_location source)
    {
        output.push_back('"');
        AppendEscapedText(output, source.file_name());
        output.push_back(':');
        AppendUnsigned(output, source.line());
        output.push_back('"');
    }

    inline void FillCachedTimeParts(
        const TimestampStyle style,
        const NGIN::Int64 second,
        std::string& prefix,
        std::string& suffix)
    {
        prefix.clear();
        suffix.clear();

        if (style == TimestampStyle::EpochNanoseconds || style == TimestampStyle::EpochMilliseconds)
        {
            return;
        }

        const std::time_t timeValue = static_cast<std::time_t>(second);
        std::tm           timeInfo {};

        if (style == TimestampStyle::Iso8601Utc)
        {
#if defined(_WIN32)
            gmtime_s(&timeInfo, &timeValue);
#else
            gmtime_r(&timeValue, &timeInfo);
#endif
            std::array<char, 32> buffer {};
            const auto count = std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &timeInfo);
            prefix.assign(buffer.data(), count);
            suffix = "Z";
        }
        else
        {
#if defined(_WIN32)
            localtime_s(&timeInfo, &timeValue);
#else
            localtime_r(&timeValue, &timeInfo);
#endif
            std::array<char, 32> prefixBuffer {};
            std::array<char, 16> suffixBuffer {};
            const auto prefixCount = std::strftime(prefixBuffer.data(), prefixBuffer.size(), "%Y-%m-%dT%H:%M:%S", &timeInfo);
            const auto suffixCount = std::strftime(suffixBuffer.data(), suffixBuffer.size(), "%z", &timeInfo);
            prefix.assign(prefixBuffer.data(), prefixCount);
            suffix.assign(suffixBuffer.data(), suffixCount);
        }
    }

    inline void AppendTimestamp(
        std::string& output,
        const NGIN::UInt64 timestampEpochNanoseconds,
        const TimestampStyle style,
        std::string_view cachedPrefix = {},
        std::string_view cachedSuffix = {})
    {
        switch (style)
        {
            case TimestampStyle::EpochNanoseconds:
                AppendUnsigned(output, timestampEpochNanoseconds);
                break;
            case TimestampStyle::EpochMilliseconds:
                AppendUnsigned(output, timestampEpochNanoseconds / 1000000ULL);
                break;
            case TimestampStyle::Iso8601Utc:
            case TimestampStyle::Iso8601Local:
            {
                const auto seconds = static_cast<NGIN::Int64>(timestampEpochNanoseconds / 1000000000ULL);
                const auto nanos = static_cast<NGIN::UInt64>(timestampEpochNanoseconds % 1000000000ULL);
                std::string prefixStorage;
                std::string suffixStorage;
                if (cachedPrefix.empty() && cachedSuffix.empty())
                {
                    FillCachedTimeParts(style, seconds, prefixStorage, suffixStorage);
                    cachedPrefix = prefixStorage;
                    cachedSuffix = suffixStorage;
                }

                AppendString(output, cachedPrefix);
                output.push_back('.');

                std::array<char, 16> fractionBuffer {};
                const auto [ptr, ec] = std::to_chars(fractionBuffer.data(), fractionBuffer.data() + fractionBuffer.size(), nanos);
                if (ec == std::errc {})
                {
                    const auto digits = static_cast<std::size_t>(ptr - fractionBuffer.data());
                    for (std::size_t i = digits; i < 9; ++i)
                    {
                        output.push_back('0');
                    }
                    output.append(fractionBuffer.data(), digits);
                }
                else
                {
                    output += "000000000";
                }

                AppendString(output, cachedSuffix);
                break;
            }
        }
    }
}
