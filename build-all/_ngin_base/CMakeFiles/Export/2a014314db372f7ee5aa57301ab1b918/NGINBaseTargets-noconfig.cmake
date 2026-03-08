#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "NGIN::BaseStatic" for configuration ""
set_property(TARGET NGIN::BaseStatic APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(NGIN::BaseStatic PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libNGINBase.a"
  )

list(APPEND _cmake_import_check_targets NGIN::BaseStatic )
list(APPEND _cmake_import_check_files_for_NGIN::BaseStatic "${_IMPORT_PREFIX}/lib/libNGINBase.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
