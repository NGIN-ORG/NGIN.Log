#pragma once

#include <NGIN/Log/LogLevel.hpp>
#include <NGIN/Log/Types.hpp>

#include <string>
#include <string_view>
#include <variant>

namespace NGIN::Log::detail
{
    inline void AppendAttributeValue(std::string& line, const AttributeValue& value)
    {
        std::visit(
            [&](const auto& v) {
                using TValue = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<TValue, bool>)
                {
                    line += v ? "true" : "false";
                }
                else if constexpr (std::is_same_v<TValue, std::string_view>)
                {
                    line.push_back('"');
                    line.append(v.data(), v.size());
                    line.push_back('"');
                }
                else
                {
                    line += std::to_string(v);
                }
            },
            value);
    }

    [[nodiscard]] inline auto FormatRecordLine(const LogRecordView& record, const bool includeSource) -> std::string
    {
        std::string line;
        line.reserve(256 + record.message.size());

        line.push_back('[');
        line += std::to_string(record.timestampEpochNanoseconds);
        line += "][";
        line += ToString(record.level);
        line += "][";
        line.append(record.loggerName.data(), record.loggerName.size());
        line += "] ";
        line.append(record.message.data(), record.message.size());

        if (!record.attributes.empty())
        {
            line += " {";
            for (std::size_t i = 0; i < record.attributes.size(); ++i)
            {
                const auto& attr = record.attributes[i];
                if (i > 0)
                {
                    line += ", ";
                }
                line.append(attr.key.data(), attr.key.size());
                line += '=';
                AppendAttributeValue(line, attr.value);
            }
            line.push_back('}');
        }

        if (record.truncatedAttributeCount > 0 || record.truncatedBytes > 0)
        {
            line += " [truncated attrs=";
            line += std::to_string(record.truncatedAttributeCount);
            line += ", bytes=";
            line += std::to_string(record.truncatedBytes);
            line += "]";
        }

        if (includeSource)
        {
            line += " (";
            line += record.source.file_name();
            line += ':';
            line += std::to_string(record.source.line());
            line += ')';
        }

        line.push_back('\n');
        return line;
    }
}
