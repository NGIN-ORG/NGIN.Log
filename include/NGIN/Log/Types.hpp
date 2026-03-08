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
    /// @brief Supported structured attribute value types.
    using AttributeValue = std::variant<NGIN::Int64, NGIN::UInt64, double, bool, std::string_view>;

    /// @brief Single key-value log attribute.
    struct LogAttribute
    {
        std::string_view key {};
        AttributeValue   value {};
    };

    /// @brief Immutable view of a log record dispatched to sinks.
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

    /// @brief Fixed-capacity inline UTF-8 text storage used by async-owned records.
    /// @tparam Capacity Maximum storable character bytes excluding null terminator.
    template<std::size_t Capacity>
    struct InlineText
    {
        /// @brief Copy bounded text into inline storage and accumulate truncated bytes.
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

        /// @brief Convert to string view.
        [[nodiscard]] auto View() const noexcept -> std::string_view
        {
            return std::string_view(buffer.data(), size);
        }

        std::array<char, Capacity + 1> buffer {};
        std::size_t                    size {0};
    };

    /// @brief Owned attribute value used by async sink queue records.
    using OwnedAttributeValue =
        std::variant<std::monostate, NGIN::Int64, NGIN::UInt64, double, bool, InlineText<Config::MaxAttrTextBytes>>;

    /// @brief Owned key-value pair used by async sink queue records.
    struct OwnedLogAttribute
    {
        InlineText<Config::MaxAttrTextBytes> key {};
        OwnedAttributeValue value {};
    };

    /// @brief Owned log record for async queue transport.
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
