#pragma once

#include <NGIN/Log/Types.hpp>

#include <chrono>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace NGIN::Log
{
    using ContextValue = std::variant<NGIN::Int64, NGIN::UInt64, double, bool, std::string>;
    struct ContextAttribute;

    namespace detail
    {
        [[nodiscard]] inline auto GetLogContextStorage() noexcept -> std::vector<ContextAttribute>&;

        template<class TValue>
        struct IsStdString : std::false_type
        {
        };

        template<class TTraits, class TAlloc>
        struct IsStdString<std::basic_string<char, TTraits, TAlloc>> : std::true_type
        {
        };

        template<class TValue>
        [[nodiscard]] auto MakeOwnedContextValue(TValue&& value) -> ContextValue
        {
            using Decayed = std::remove_cvref_t<TValue>;

            if constexpr (std::same_as<Decayed, ContextValue>)
            {
                return std::forward<TValue>(value);
            }
            else if constexpr (std::same_as<Decayed, AttributeValue>)
            {
                return std::visit(
                    [](const auto& attributeValue) -> ContextValue {
                        using AttributeType = std::decay_t<decltype(attributeValue)>;
                        if constexpr (std::same_as<AttributeType, std::string_view>)
                        {
                            return std::string(attributeValue);
                        }
                        else
                        {
                            return attributeValue;
                        }
                    },
                    value);
            }
            else if constexpr (std::same_as<Decayed, bool>)
            {
                return value;
            }
            else if constexpr (std::is_enum_v<Decayed>)
            {
                return MakeOwnedContextValue(static_cast<std::underlying_type_t<Decayed>>(value));
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
                return std::string(value);
            }
            else if constexpr (std::same_as<Decayed, const char*> || std::same_as<Decayed, char*>)
            {
                return std::string(value != nullptr ? value : "");
            }
            else if constexpr (IsStdString<Decayed>::value)
            {
                return std::string(std::forward<TValue>(value));
            }
            else if constexpr (requires { std::string_view {value}; })
            {
                return std::string(std::string_view {value});
            }
            else if constexpr (requires { typename Decayed::rep; typename Decayed::period; } &&
                               requires { std::chrono::duration_cast<std::chrono::nanoseconds>(value); })
            {
                return static_cast<NGIN::Int64>(std::chrono::duration_cast<std::chrono::nanoseconds>(value).count());
            }
            else if constexpr (std::same_as<Decayed, std::error_code>)
            {
                return std::string(value.category().name()) + ":" + std::to_string(value.value()) + ":" + value.message();
            }
            else if constexpr (std::is_pointer_v<Decayed>)
            {
                std::array<char, sizeof(void*) * 2 + 3> buffer {};
                buffer[0] = '0';
                buffer[1] = 'x';
                const auto raw = reinterpret_cast<std::uintptr_t>(value);
                const auto [ptr, ec] = std::to_chars(buffer.data() + 2, buffer.data() + buffer.size(), raw, 16);
                if (ec == std::errc {})
                {
                    return std::string(buffer.data(), static_cast<std::size_t>(ptr - buffer.data()));
                }

                return std::string("0x0");
            }
            else
            {
                static_assert(sizeof(Decayed) == 0, "Unsupported context value type");
            }
        }
    }

    struct ContextAttribute
    {
        std::string  key {};
        ContextValue value {};

        ContextAttribute() = default;

        template<class TValue>
        ContextAttribute(std::string_view inputKey, TValue&& inputValue)
            : key(inputKey)
            , value(detail::MakeOwnedContextValue(std::forward<TValue>(inputValue)))
        {
        }
    };

    class ScopedLogContext
    {
    public:
        ScopedLogContext() = default;

        ScopedLogContext(const ScopedLogContext&) = delete;
        auto operator=(const ScopedLogContext&) -> ScopedLogContext& = delete;

        ScopedLogContext(ScopedLogContext&& other) noexcept
            : m_start(other.m_start)
            , m_active(other.m_active)
        {
            other.m_active = false;
        }

        auto operator=(ScopedLogContext&& other) noexcept -> ScopedLogContext&
        {
            if (this != &other)
            {
                Reset();
                m_start = other.m_start;
                m_active = other.m_active;
                other.m_active = false;
            }

            return *this;
        }

        ~ScopedLogContext()
        {
            Reset();
        }

    private:
        explicit ScopedLogContext(const std::size_t start) noexcept
            : m_start(start)
        {
        }

        void Reset() noexcept
        {
            if (m_active)
            {
                detail::GetLogContextStorage().resize(m_start);
                m_active = false;
            }
        }

        std::size_t m_start {0};
        bool        m_active {true};

        friend auto PushLogContext(std::initializer_list<ContextAttribute> attributes) -> ScopedLogContext;
    };

    namespace detail
    {
        [[nodiscard]] inline auto GetLogContextStorage() noexcept -> std::vector<ContextAttribute>&
        {
            static thread_local std::vector<ContextAttribute> contextStorage {};
            return contextStorage;
        }

        [[nodiscard]] inline auto GetLogContextView() noexcept -> std::span<const ContextAttribute>
        {
            const auto& storage = GetLogContextStorage();
            return std::span<const ContextAttribute>(storage.data(), storage.size());
        }
    }

    [[nodiscard]] inline auto PushLogContext(std::initializer_list<ContextAttribute> attributes) -> ScopedLogContext
    {
        auto& storage = detail::GetLogContextStorage();
        const auto start = storage.size();
        storage.reserve(storage.size() + attributes.size());
        for (const auto& attribute : attributes)
        {
            storage.push_back(attribute);
        }

        return ScopedLogContext(start);
    }
}
