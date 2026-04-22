#pragma once

#include <NGIN/Log/Config.hpp>
#include <NGIN/Log/Types.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <concepts>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace NGIN::Log
{
    class RecordBuilder
    {
    public:
        RecordBuilder() noexcept
        {
            m_messageBuffer[0] = '\0';
        }

        void Message(const std::string_view message) noexcept
        {
            const auto copyLength = std::min(message.size(), Config::MaxMessageBytes);
            if (copyLength > 0)
            {
                std::memcpy(m_messageBuffer.data(), message.data(), copyLength);
            }
            m_messageSize = copyLength;
            m_messageBuffer[m_messageSize] = '\0';

            if (message.size() > copyLength)
            {
                m_truncatedBytes += static_cast<NGIN::UInt32>(message.size() - copyLength);
            }
        }

        template<class... Args>
        void Format(std::format_string<Args...> fmt, Args&&... args) noexcept
        {
            try
            {
                const auto result = std::format_to_n(
                    m_messageBuffer.data(),
                    Config::MaxMessageBytes,
                    fmt,
                    std::forward<Args>(args)...);

                const auto formattedSize = static_cast<std::size_t>(result.size);
                m_messageSize = std::min(formattedSize, Config::MaxMessageBytes);
                m_messageBuffer[m_messageSize] = '\0';

                if (formattedSize > Config::MaxMessageBytes)
                {
                    m_truncatedBytes += static_cast<NGIN::UInt32>(formattedSize - Config::MaxMessageBytes);
                }
            }
            catch (...)
            {
                Message("[format-error]");
            }
        }

        void VFormat(const std::string_view fmt, std::format_args args) noexcept
        {
            try
            {
                Message(std::vformat(fmt, args));
            }
            catch (...)
            {
                Message("[format-error]");
            }
        }

        void Attr(const std::string_view key, const AttributeValue& value) noexcept
        {
            AppendAttribute(key, CopyAttributeValue(value));
        }

        template<class TValue>
        void Attr(const std::string_view key, TValue&& value) noexcept
        {
            AppendAttribute(key, NormalizeValue(std::forward<TValue>(value)));
        }

        [[nodiscard]] auto GetMessage() const noexcept -> std::string_view
        {
            return std::string_view(m_messageBuffer.data(), m_messageSize);
        }

        [[nodiscard]] auto GetAttributes() const noexcept -> std::span<const LogAttribute>
        {
            return std::span<const LogAttribute>(m_attributes.data(), m_attributeCount);
        }

        [[nodiscard]] auto GetTruncatedAttributeCount() const noexcept -> NGIN::UInt32
        {
            return m_truncatedAttributeCount;
        }

        [[nodiscard]] auto GetTruncatedBytes() const noexcept -> NGIN::UInt32
        {
            return m_truncatedBytes;
        }

    private:
        [[nodiscard]] auto CopyText(const std::string_view input) noexcept -> std::string_view
        {
            if (input.empty())
            {
                return {};
            }

            const auto available = Config::MaxAttrTextBytes - m_attrTextUsed;
            if (available == 0)
            {
                m_truncatedBytes += static_cast<NGIN::UInt32>(input.size());
                return {};
            }

            const auto copyLength = std::min(input.size(), available);
            std::memcpy(m_attributeTextBuffer.data() + m_attrTextUsed, input.data(), copyLength);

            const auto view = std::string_view(m_attributeTextBuffer.data() + m_attrTextUsed, copyLength);
            m_attrTextUsed += copyLength;

            if (input.size() > copyLength)
            {
                m_truncatedBytes += static_cast<NGIN::UInt32>(input.size() - copyLength);
            }

            return view;
        }

        [[nodiscard]] auto FormatPointerText(const void* value) noexcept -> std::string_view
        {
            std::array<char, sizeof(void*) * 2 + 3> buffer {};
            buffer[0] = '0';
            buffer[1] = 'x';

            const auto raw = reinterpret_cast<std::uintptr_t>(value);
            auto*      begin = buffer.data() + 2;
            auto*      end = buffer.data() + buffer.size();
            const auto [ptr, ec] = std::to_chars(begin, end, raw, 16);
            if (ec != std::errc {})
            {
                return CopyText("0x0");
            }

            return CopyText(std::string_view(buffer.data(), static_cast<std::size_t>(ptr - buffer.data())));
        }

        [[nodiscard]] auto FormatErrorCodeText(const std::error_code& value) noexcept -> std::string_view
        {
            try
            {
                const std::string text =
                    std::string(value.category().name()) + ":" + std::to_string(value.value()) + ":" + value.message();
                return CopyText(text);
            }
            catch (...)
            {
                return CopyText("[error-code-format-error]");
            }
        }

        [[nodiscard]] auto CopyAttributeValue(const AttributeValue& value) noexcept -> AttributeValue
        {
            return std::visit(
                [this](const auto& attributeValue) -> AttributeValue {
                    using AttributeType = std::decay_t<decltype(attributeValue)>;
                    if constexpr (std::same_as<AttributeType, std::string_view>)
                    {
                        return CopyText(attributeValue);
                    }
                    else
                    {
                        return attributeValue;
                    }
                },
                value);
        }

        template<class TValue>
        [[nodiscard]] auto NormalizeValue(TValue&& value) noexcept -> AttributeValue
        {
            using Decayed = std::remove_cvref_t<TValue>;

            if constexpr (std::same_as<Decayed, AttributeValue>)
            {
                return CopyAttributeValue(value);
            }
            else if constexpr (std::same_as<Decayed, bool>)
            {
                return value;
            }
            else if constexpr (std::is_enum_v<Decayed>)
            {
                return NormalizeValue(static_cast<std::underlying_type_t<Decayed>>(value));
            }
            else if constexpr (std::integral<Decayed> && std::signed_integral<Decayed>)
            {
                return static_cast<NGIN::Int64>(value);
            }
            else if constexpr (std::integral<Decayed> && std::unsigned_integral<Decayed>)
            {
                return static_cast<NGIN::UInt64>(value);
            }
            else if constexpr (std::floating_point<Decayed>)
            {
                return static_cast<double>(value);
            }
            else if constexpr (std::same_as<Decayed, std::string_view>)
            {
                return CopyText(value);
            }
            else if constexpr (std::same_as<Decayed, const char*> || std::same_as<Decayed, char*>)
            {
                return CopyText(value != nullptr ? std::string_view(value) : std::string_view {});
            }
            else if constexpr (std::same_as<Decayed, std::string>)
            {
                return CopyText(value);
            }
            else if constexpr (requires { std::string_view {value}; })
            {
                return CopyText(std::string_view {value});
            }
            else if constexpr (requires { typename Decayed::rep; typename Decayed::period; } &&
                               requires { std::chrono::duration_cast<std::chrono::nanoseconds>(value); })
            {
                return static_cast<NGIN::Int64>(std::chrono::duration_cast<std::chrono::nanoseconds>(value).count());
            }
            else if constexpr (std::same_as<Decayed, std::error_code>)
            {
                return FormatErrorCodeText(value);
            }
            else if constexpr (std::is_pointer_v<Decayed>)
            {
                return FormatPointerText(static_cast<const void*>(value));
            }
            else
            {
                static_assert(sizeof(Decayed) == 0, "Unsupported attribute type");
            }
        }

        void AppendAttribute(const std::string_view key, const AttributeValue& value) noexcept
        {
            if (m_attributeCount >= Config::MaxAttributes)
            {
                ++m_truncatedAttributeCount;
                return;
            }

            m_attributes[m_attributeCount].key = CopyText(key);
            m_attributes[m_attributeCount].value = value;
            ++m_attributeCount;
        }

        std::array<char, Config::MaxMessageBytes + 1>   m_messageBuffer {};
        std::size_t                                     m_messageSize {0};
        std::array<char, Config::MaxAttrTextBytes>      m_attributeTextBuffer {};
        std::size_t                                     m_attrTextUsed {0};
        std::array<LogAttribute, Config::MaxAttributes> m_attributes {};
        std::size_t                                     m_attributeCount {0};
        NGIN::UInt32                                    m_truncatedAttributeCount {0};
        NGIN::UInt32                                    m_truncatedBytes {0};
    };
}
