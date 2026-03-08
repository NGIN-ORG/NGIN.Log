#pragma once

#include <NGIN/Log/RecordBuilder.hpp>

#include <format>
#include <utility>

namespace NGIN::Log
{
    /// @brief Default formatting policy using std::format.
    struct StdFormatter
    {
        /// @brief Format into a RecordBuilder message buffer.
        template<class... Args>
        static void Format(RecordBuilder& builder, std::format_string<Args...> fmt, Args&&... args) noexcept
        {
            builder.Format(fmt, std::forward<Args>(args)...);
        }

        /// @brief Format into a RecordBuilder message buffer using runtime format args.
        static void FormatV(RecordBuilder& builder, const std::string_view fmt, const std::format_args args) noexcept
        {
            builder.VFormat(fmt, args);
        }
    };
}
