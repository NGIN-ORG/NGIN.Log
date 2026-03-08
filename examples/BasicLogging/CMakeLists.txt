cmake_minimum_required(VERSION 3.20)

add_executable(NGINLogBasicExample main.cpp)
target_compile_features(NGINLogBasicExample PRIVATE cxx_std_23)
target_link_libraries(NGINLogBasicExample PRIVATE NGIN::Log)
if(TARGET NGIN.Log.BuildOptions)
  target_link_libraries(NGINLogBasicExample PRIVATE NGIN.Log.BuildOptions)
endif()
set_target_properties(NGINLogBasicExample PROPERTIES FOLDER "Examples")
