#pragma once

#include <NGIN/Log/Config.hpp>
#include <NGIN/Log/Types.hpp>
#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <span>
#include <string_view>
#include <utility>

namespace NGIN::Log
{
    /// @brief Stack-only bounded record construction helper.
    class RecordBuilder
    {
    public:
        RecordBuilder() noexcept
        {
            m_messageBuffer[0] = '\0';
        }

        /// @brief Set message text directly.
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

        /// @brief Format message text into bounded storage.
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

        /// @brief Format message text from runtime format arguments.
        /// @note This path may allocate internally in `std::vformat`.
        void VFormat(const std::string_view fmt, std::format_args args) noexcept
        {
            try
            {
                // Route through Message to preserve bounded-copy truncation accounting.
                Message(std::vformat(fmt, args));
            }
            catch (...)
            {
                Message("[format-error]");
            }
        }

        /// @brief Append a structured attribute.
        void Attr(const std::string_view key, const AttributeValue& value) noexcept
        {
            if (m_attributeCount >= Config::MaxAttributes)
            {
                ++m_truncatedAttributeCount;
                return;
            }

            const auto copiedKey = CopyText(key);
            auto       storedValue = value;

            if (std::holds_alternative<std::string_view>(storedValue))
            {
                storedValue = CopyText(std::get<std::string_view>(storedValue));
            }

            m_attributes[m_attributeCount].key = copiedKey;
            m_attributes[m_attributeCount].value = storedValue;
            ++m_attributeCount;
        }

        /// @brief Get current message view.
        [[nodiscard]] auto GetMessage() const noexcept -> std::string_view
        {
            return std::string_view(m_messageBuffer.data(), m_messageSize);
        }

        /// @brief Get current attribute span.
        [[nodiscard]] auto GetAttributes() const noexcept -> std::span<const LogAttribute>
        {
            return std::span<const LogAttribute>(m_attributes.data(), m_attributeCount);
        }

        /// @brief Number of dropped attributes due to capacity.
        [[nodiscard]] auto GetTruncatedAttributeCount() const noexcept -> NGIN::UInt32
        {
            return m_truncatedAttributeCount;
        }

        /// @brief Number of dropped bytes due to bounded buffers.
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

        std::array<char, Config::MaxMessageBytes + 1> m_messageBuffer {};
        std::size_t                                   m_messageSize {0};

        std::array<char, Config::MaxAttrTextBytes>    m_attributeTextBuffer {};
        std::size_t                                   m_attrTextUsed {0};

        std::array<LogAttribute, Config::MaxAttributes> m_attributes {};
        std::size_t                                     m_attributeCount {0};

        NGIN::UInt32 m_truncatedAttributeCount {0};
        NGIN::UInt32 m_truncatedBytes {0};
    };
}
