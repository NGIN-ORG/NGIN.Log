#pragma once

#include <source_location>

#define NGIN_LOG_TRACEF(logger, ...) (logger).Tracef(std::source_location::current(), __VA_ARGS__)
#define NGIN_LOG_DEBUGF(logger, ...) (logger).Debugf(std::source_location::current(), __VA_ARGS__)
#define NGIN_LOG_INFOF(logger, ...)  (logger).Infof(std::source_location::current(), __VA_ARGS__)
#define NGIN_LOG_WARNF(logger, ...)  (logger).Warnf(std::source_location::current(), __VA_ARGS__)
#define NGIN_LOG_ERRORF(logger, ...) (logger).Errorf(std::source_location::current(), __VA_ARGS__)
#define NGIN_LOG_FATALF(logger, ...) (logger).Fatalf(std::source_location::current(), __VA_ARGS__)
