cmake_minimum_required(VERSION 3.20)
project(NGIN.Log VERSION 0.1 LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

option(NGIN_LOG_BUILD_STATIC "Build NGIN.Log as a static library" ON)
option(NGIN_LOG_BUILD_SHARED "Build NGIN.Log as a shared library" OFF)
option(NGIN_LOG_BUILD_TESTS "Build NGIN.Log tests" ON)
option(NGIN_LOG_BUILD_EXAMPLES "Build NGIN.Log examples" ON)
option(NGIN_LOG_BUILD_BENCHMARKS "Build NGIN.Log benchmarks" ON)
option(NGIN_LOG_ENABLE_ASAN "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_LOG_ENABLE_TSAN "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_LOG_ENABLE_LTO "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_LOG_STRICT_WARNINGS "Enable extra warning flags" ON)
option(NGIN_LOG_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)

if(NGIN_LOG_ENABLE_ASAN AND NGIN_LOG_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

if(NOT NGIN_LOG_BUILD_STATIC AND NOT NGIN_LOG_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_LOG_EXPORT_COMPILE_COMMANDS})

function(ngin_log_enable_warnings tgt)
  if(NGIN_LOG_STRICT_WARNINGS)
    get_target_property(_ngin_log_target_type ${tgt} TYPE)
    set(_ngin_log_visibility PRIVATE)
    if(_ngin_log_target_type STREQUAL "INTERFACE_LIBRARY")
      set(_ngin_log_visibility INTERFACE)
    endif()
    if(MSVC)
      target_compile_options(${tgt} ${_ngin_log_visibility} /W4 /permissive-)
    else()
      target_compile_options(${tgt} ${_ngin_log_visibility} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    endif()
  endif()
endfunction()

#-------------------------------------------------------------------------------
# Locate NGIN.Base
#-------------------------------------------------------------------------------
if(NOT TARGET NGIN::Base)
  find_package(NGINBase CONFIG QUIET)
  if(NOT NGINBase_FOUND)
    set(_base_dir "${CMAKE_CURRENT_LIST_DIR}/../NGIN.Base")
    if(EXISTS "${_base_dir}/CMakeLists.txt")
      message(STATUS "NGIN.Log: using sibling NGIN.Base from ${_base_dir}")
      set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_base_dir}" _ngin_base EXCLUDE_FROM_ALL)
    elseif(DEFINED NGIN_BASE_SOURCE_DIR)
      message(STATUS "NGIN.Log: using NGIN.Base from ${NGIN_BASE_SOURCE_DIR}")
      set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${NGIN_BASE_SOURCE_DIR}" _ngin_base EXCLUDE_FROM_ALL)
    else()
      message(FATAL_ERROR "NGIN.Base not found. Install NGINBase or set NGIN_BASE_SOURCE_DIR to the source path.")
    endif()
  endif()
endif()

add_library(NGIN.Log.BuildOptions INTERFACE)
add_library(NGIN::Log::BuildOptions ALIAS NGIN.Log.BuildOptions)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NGIN_LOG_ENABLE_ASAN)
    target_compile_options(NGIN.Log.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=address,undefined>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Log.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=address,undefined>)
  endif()
  if(NGIN_LOG_ENABLE_TSAN)
    target_compile_options(NGIN.Log.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=thread>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Log.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=thread>)
  endif()
endif()

set(NGIN_LOG_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Log/LoggerRegistry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Log/Sinks/ConsoleSink.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Log/Sinks/FileSink.cpp
)

set(NGIN_LOG_EXPORT_TARGETS)
set(ngin_log_primary_target "")

if(NGIN_LOG_BUILD_STATIC OR NGIN_LOG_BUILD_SHARED)
  add_library(NGIN.Log.Object OBJECT ${NGIN_LOG_SOURCES})
  target_compile_features(NGIN.Log.Object PRIVATE cxx_std_23)
  set_target_properties(NGIN.Log.Object PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Log.Object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(NGIN.Log.Object PRIVATE NGIN::Base NGIN.Log.BuildOptions)
  ngin_log_enable_warnings(NGIN.Log.Object)
endif()

if(NGIN_LOG_BUILD_SHARED AND TARGET NGIN.Log.Object)
  set_property(TARGET NGIN.Log.Object PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if(NGIN_LOG_BUILD_STATIC)
  add_library(NGIN.Log.Static STATIC)
  if(TARGET NGIN.Log.Object)
    target_sources(NGIN.Log.Static PRIVATE $<TARGET_OBJECTS:NGIN.Log.Object>)
  endif()
  target_compile_features(NGIN.Log.Static PUBLIC cxx_std_23)
  set_target_properties(NGIN.Log.Static PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Log.Static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Log.Static PUBLIC NGIN::Base)
  target_link_libraries(NGIN.Log.Static PRIVATE NGIN.Log.BuildOptions)
  set_target_properties(NGIN.Log.Static PROPERTIES OUTPUT_NAME "NGINLog" EXPORT_NAME LogStatic)
  target_compile_definitions(NGIN.Log.Static PUBLIC NGIN_LOG_STATIC)
  ngin_log_enable_warnings(NGIN.Log.Static)
  list(APPEND NGIN_LOG_EXPORT_TARGETS NGIN.Log.Static)
  add_library(NGIN::Log::Static ALIAS NGIN.Log.Static)
  set(ngin_log_primary_target NGIN.Log.Static)
endif()

if(NGIN_LOG_BUILD_SHARED)
  add_library(NGIN.Log.Shared SHARED)
  if(TARGET NGIN.Log.Object)
    target_sources(NGIN.Log.Shared PRIVATE $<TARGET_OBJECTS:NGIN.Log.Object>)
  endif()
  target_compile_features(NGIN.Log.Shared PUBLIC cxx_std_23)
  set_target_properties(NGIN.Log.Shared PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Log.Shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Log.Shared PUBLIC NGIN::Base)
  target_link_libraries(NGIN.Log.Shared PRIVATE NGIN.Log.BuildOptions)
  target_compile_definitions(NGIN.Log.Shared PUBLIC NGIN_LOG_SHARED)
  target_compile_definitions(NGIN.Log.Shared PRIVATE NGIN_LOG_SHARED_BUILD)
  set_target_properties(NGIN.Log.Shared PROPERTIES OUTPUT_NAME "NGINLog" EXPORT_NAME LogShared)
  if(NOT WIN32)
    set_target_properties(NGIN.Log.Shared PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})
  endif()
  ngin_log_enable_warnings(NGIN.Log.Shared)
  list(APPEND NGIN_LOG_EXPORT_TARGETS NGIN.Log.Shared)
  add_library(NGIN::Log::Shared ALIAS NGIN.Log.Shared)
  set(ngin_log_primary_target NGIN.Log.Shared)
endif()

if(NOT ngin_log_primary_target)
  message(FATAL_ERROR "No primary NGIN.Log target could be determined.")
endif()

add_library(NGIN::Log ALIAS ${ngin_log_primary_target})

if(NGIN_LOG_ENABLE_LTO)
  if(NGIN_LOG_ENABLE_ASAN OR NGIN_LOG_ENABLE_TSAN)
    message(WARNING "LTO requested but sanitizers are enabled; skipping IPO/LTO.")
  else()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NGIN_LOG_HAS_IPO OUTPUT NGIN_LOG_IPO_ERR)
    if(NGIN_LOG_HAS_IPO)
      foreach(tgt IN ITEMS NGIN.Log.Object NGIN.Log.Static NGIN.Log.Shared)
        if(TARGET ${tgt})
          foreach(cfg IN ITEMS Release RelWithDebInfo MinSizeRel)
            set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
          endforeach()
        endif()
      endforeach()
    else()
      message(WARNING "LTO requested but not supported: ${NGIN_LOG_IPO_ERR}")
    endif()
  endif()
endif()

set(_ngin_log_need_atomic FALSE)
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  include(CheckCXXSourceCompiles)
  string(CONCAT _ngin_log_atomic_test_source
    "#include <atomic>\n"
    "#include <cstddef>\n"
    "int main() {\n"
    "  std::atomic<std::size_t> v {0};\n"
    "  v.fetch_add(1, std::memory_order_relaxed);\n"
    "  return static_cast<int>(v.load(std::memory_order_relaxed));\n"
    "}\n")
  set(_ngin_log_required_quiet_prev "${CMAKE_REQUIRED_QUIET}")
  set(CMAKE_REQUIRED_QUIET TRUE)
  check_cxx_source_compiles("${_ngin_log_atomic_test_source}" NGIN_LOG_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
  if(NOT NGIN_LOG_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
    set(CMAKE_REQUIRED_LIBRARIES atomic)
    check_cxx_source_compiles("${_ngin_log_atomic_test_source}" NGIN_LOG_ATOMIC_LINKS_WITH_LIBATOMIC)
    if(NGIN_LOG_ATOMIC_LINKS_WITH_LIBATOMIC)
      set(_ngin_log_need_atomic TRUE)
    endif()
    unset(CMAKE_REQUIRED_LIBRARIES)
  endif()
  set(CMAKE_REQUIRED_QUIET "${_ngin_log_required_quiet_prev}")
  unset(_ngin_log_required_quiet_prev)
endif()

if(_ngin_log_need_atomic)
  if(TARGET NGIN.Log.Static)
    target_link_libraries(NGIN.Log.Static PRIVATE atomic)
  endif()
  if(TARGET NGIN.Log.Shared)
    target_link_libraries(NGIN.Log.Shared PRIVATE atomic)
  endif()
endif()

include(GNUInstallDirs)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  TARGETS ${NGIN_LOG_EXPORT_TARGETS}
  EXPORT NGINLogTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set_target_properties(NGIN.Log.BuildOptions PROPERTIES EXPORT_NAME LogBuildOptions)
install(TARGETS NGIN.Log.BuildOptions EXPORT NGINLogTargets)

install(
  EXPORT NGINLogTargets
  FILE NGINLogTargets.cmake
  NAMESPACE NGIN::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINLog
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NGINLogConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINLogConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NGINLogConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINLog
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NGINLogConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINLogConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINLog
)

if(NGIN_LOG_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NGIN_LOG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NGIN_LOG_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
