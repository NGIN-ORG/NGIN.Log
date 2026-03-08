# cmake/NGINBaseConfig.cmake.in

####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was NGINBaseConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

# Allow downstream to find any dependencies you add in the future
include(CMakeFindDependencyMacro)

# Bring in the exported targets
include("${CMAKE_CURRENT_LIST_DIR}/NGINBaseTargets.cmake")

# Provide canonical aliases for consumers
if(TARGET NGIN::BaseStatic AND NOT TARGET NGIN::Base::Static)
  add_library(NGIN::Base::Static ALIAS NGIN::BaseStatic)
endif()

if(TARGET NGIN::BaseShared AND NOT TARGET NGIN::Base::Shared)
  add_library(NGIN::Base::Shared ALIAS NGIN::BaseShared)
endif()

set(_ngin_base_primary_target "")
if(TARGET NGIN::BaseShared)
  set(_ngin_base_primary_target NGIN::BaseShared)
elseif(TARGET NGIN::BaseStatic)
  set(_ngin_base_primary_target NGIN::BaseStatic)
endif()

if(_ngin_base_primary_target AND NOT TARGET NGIN::Base)
  add_library(NGIN::Base ALIAS ${_ngin_base_primary_target})
endif()

# Sanity check: ensure the primary alias exists
if(NOT TARGET NGIN::Base)
  message(FATAL_ERROR "NGIN::Base target not found. "
                      "Something went wrong with NGIN.Base installation.")
endif()
