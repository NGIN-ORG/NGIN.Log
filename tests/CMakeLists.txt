cmake_minimum_required(VERSION 3.20)
project(NGINLogTests LANGUAGES CXX)

set(_log_root_dir "${NGIN_LOG_ROOT_DIR}")
if(NOT _log_root_dir)
  get_filename_component(_log_root_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

include(CTest)

set(_have_catch2 OFF)
set(_catch2_extra_dir "")

if(TARGET Catch2::Catch2WithMain)
  set(_have_catch2 ON)
endif()

if(NOT _have_catch2)
  find_package(Catch2 3 CONFIG QUIET)
  if(Catch2_FOUND AND TARGET Catch2::Catch2WithMain)
    set(_have_catch2 ON)
  endif()
endif()

if(NOT _have_catch2)
  set(_catch2_local_candidates
    "${_log_root_dir}/../NGIN.Base/build/tests/_deps/catch2-src"
    "${_log_root_dir}/../NGIN.Base/build/_deps/catch2-src"
    "${_log_root_dir}/../NGIN.Reflection/build/tests/_deps/catch2-src"
    "${_log_root_dir}/../NGIN.Reflection/build/_deps/catch2-src"
  )

  foreach(_cand IN LISTS _catch2_local_candidates)
    if(EXISTS "${_cand}/CMakeLists.txt")
      add_subdirectory("${_cand}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2" EXCLUDE_FROM_ALL)
      if(TARGET Catch2::Catch2WithMain)
        set(_have_catch2 ON)
        set(_catch2_extra_dir "${_cand}/extras")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT _have_catch2)
  include("${_log_root_dir}/cmake/CPM.cmake")
  set(CPM_SOURCE_CACHE "${PROJECT_BINARY_DIR}/cpm-cache" CACHE PATH "CPM cache")

  CPMAddPackage(
    NAME Catch2
    VERSION 3.5.3
    GITHUB_REPOSITORY catchorg/Catch2
    OPTIONS
      "CATCH_ENABLE_COVERAGE OFF"
      "CATCH_ENABLE_WERROR OFF"
      "CATCH_INSTALL_DOCS OFF"
      "CATCH_INSTALL_EXTRAS OFF"
  )

  if(Catch2_SOURCE_DIR)
    set(_catch2_extra_dir "${Catch2_SOURCE_DIR}/extras")
  endif()

  if(TARGET Catch2::Catch2WithMain)
    set(_have_catch2 ON)
  endif()
endif()

if(NOT _have_catch2)
  message(FATAL_ERROR "Catch2::Catch2WithMain was not found or created")
endif()

if(_catch2_extra_dir)
  list(APPEND CMAKE_MODULE_PATH "${_catch2_extra_dir}")
endif()
include(Catch)

add_executable(NGINLogTests AllTests.cpp)
target_compile_features(NGINLogTests PRIVATE cxx_std_23)
target_link_libraries(NGINLogTests PRIVATE Catch2::Catch2WithMain NGIN::Log)
if(TARGET NGIN.Log.BuildOptions)
  target_link_libraries(NGINLogTests PRIVATE NGIN.Log.BuildOptions)
endif()

if(MSVC)
  target_compile_options(NGINLogTests PRIVATE /W4 /permissive-)
else()
  target_compile_options(NGINLogTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(NGINLogTests
  TEST_PREFIX "Log."
  WORKING_DIRECTORY $<TARGET_FILE_DIR:NGINLogTests>)
