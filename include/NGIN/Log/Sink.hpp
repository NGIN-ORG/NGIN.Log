#pragma once

#include <NGIN/Log/Export.hpp>
#include <NGIN/Log/Types.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <concepts>
#include <type_traits>
#include <utility>

namespace NGIN::Log
{
    /// @brief Interface implemented by concrete log output sinks.
    class NGIN_LOG_API ILogSink
    {
    public:
        virtual ~ILogSink() = default;

        /// @brief Consume one immutable record view.
        /// @param record Structured record payload.
        virtual void Write(const LogRecordView& record) noexcept = 0;

        /// @brief Flush buffered sink state.
        virtual void Flush() noexcept = 0;
    };

    /// @brief Sink that discards all records.
    class NGIN_LOG_API NullSink final : public ILogSink
    {
    public:
        void Write(const LogRecordView&) noexcept override {}
        void Flush() noexcept override {}
    };

    /// @brief Shared sink pointer used by logger fan-out.
    using SinkPtr = NGIN::Memory::Shared<ILogSink>;

    /// @brief Construct a shared polymorphic sink instance.
    /// @tparam TSink Concrete sink type deriving from ILogSink.
    /// @param args Constructor arguments forwarded to TSink.
    /// @return Empty on allocation/construction failure.
    template<class TSink, class... Args>
        requires std::derived_from<TSink, ILogSink>
    [[nodiscard]] auto MakeSink(Args&&... args) noexcept -> SinkPtr
    {
        try
        {
            return NGIN::Memory::MakeSharedAs<ILogSink, TSink>(std::forward<Args>(args)...);
        }
        catch (...)
        {
            return {};
        }
    }
}
