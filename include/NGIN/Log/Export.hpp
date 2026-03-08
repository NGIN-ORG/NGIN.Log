#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(NGIN_LOG_STATIC)
    #define NGIN_LOG_API
  #elif defined(NGIN_LOG_SHARED_BUILD)
    #define NGIN_LOG_API __declspec(dllexport)
  #elif defined(NGIN_LOG_SHARED)
    #define NGIN_LOG_API __declspec(dllimport)
  #else
    #define NGIN_LOG_API
  #endif
  #define NGIN_LOG_LOCAL
#else
  #if defined(NGIN_LOG_SHARED_BUILD) || defined(NGIN_LOG_SHARED)
    #define NGIN_LOG_API __attribute__((visibility("default")))
  #else
    #define NGIN_LOG_API
  #endif
  #define NGIN_LOG_LOCAL __attribute__((visibility("hidden")))
#endif

#ifndef NGIN_LOG_LOCAL
#define NGIN_LOG_LOCAL
#endif
