#pragma once

#include <NGIN/Log/Config.hpp>
#include <NGIN/Log/LogLevel.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <source_location>
#include <span>
#include <string_view>
#include <variant>

namespace NGIN::Log
{
    using AttributeValue = std::variant<NGIN::Int64, NGIN::UInt64, double, bool, std::string_view>;

    struct LogAttribute
    {
        std::string_view key {};
        AttributeValue   value {};
    };

    struct LogRecordView
    {
        NGIN::UInt64                  timestampEpochNanoseconds {0};
        LogLevel                      level {LogLevel::Info};
        std::string_view              loggerName {};
        std::string_view              message {};
        std::source_location          source = std::source_location::current();
        NGIN::UInt64                  threadIdHash {0};
        std::span<const LogAttribute> attributes {};
        NGIN::UInt32                  truncatedAttributeCount {0};
        NGIN::UInt32                  truncatedBytes {0};
    };

    template<std::size_t Capacity>
    struct InlineText
    {
        void Assign(const std::string_view input, NGIN::UInt32& truncatedBytes) noexcept
        {
            const auto copyLength = std::min<std::size_t>(input.size(), Capacity);
            if (copyLength > 0)
            {
                std::memcpy(buffer.data(), input.data(), copyLength);
            }

            size = copyLength;
            buffer[size] = '\0';

            if (input.size() > copyLength)
            {
                truncatedBytes += static_cast<NGIN::UInt32>(input.size() - copyLength);
            }
        }

        [[nodiscard]] auto View() const noexcept -> std::string_view
        {
            return std::string_view(buffer.data(), size);
        }

        std::array<char, Capacity + 1> buffer {};
        std::size_t                    size {0};
    };

    using OwnedAttributeValue =
        std::variant<std::monostate, NGIN::Int64, NGIN::UInt64, double, bool, InlineText<Config::MaxAttrTextBytes>>;

    struct OwnedLogAttribute
    {
        InlineText<Config::MaxAttrTextBytes> key {};
        OwnedAttributeValue                  value {};
    };

    struct OwnedLogRecord
    {
        NGIN::UInt64                                         timestampEpochNanoseconds {0};
        LogLevel                                             level {LogLevel::Info};
        InlineText<Config::MaxMessageBytes>                  loggerName {};
        InlineText<Config::MaxMessageBytes>                  message {};
        std::source_location                                 source = std::source_location::current();
        NGIN::UInt64                                         threadIdHash {0};
        std::array<OwnedLogAttribute, Config::MaxAttributes> attributes {};
        std::size_t                                          attributeCount {0};
        NGIN::UInt32                                         truncatedAttributeCount {0};
        NGIN::UInt32                                         truncatedBytes {0};
    };
}
