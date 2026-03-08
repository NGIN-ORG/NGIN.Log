#pragma once

#include <cstddef>

namespace NGIN::Log::Config
{
    /// @brief Maximum message bytes stored in RecordBuilder.
    inline constexpr std::size_t MaxMessageBytes    = 512;

    /// @brief Maximum number of structured attributes per record.
    inline constexpr std::size_t MaxAttributes      = 8;

    /// @brief Total bytes reserved for inline attribute string storage.
    inline constexpr std::size_t MaxAttrTextBytes   = 256;

    /// @brief Default asynchronous sink queue capacity.
    inline constexpr std::size_t AsyncQueueCapacity = 8192;
}
