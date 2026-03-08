cmake_minimum_required(VERSION 3.20)

add_executable(NGINLogBenchmarks LoggingBenchmarks.cpp)
target_compile_features(NGINLogBenchmarks PRIVATE cxx_std_23)
target_link_libraries(NGINLogBenchmarks PRIVATE NGIN::Log)
if(TARGET NGIN.Log.BuildOptions)
  target_link_libraries(NGINLogBenchmarks PRIVATE NGIN.Log.BuildOptions)
endif()
set_target_properties(NGINLogBenchmarks PROPERTIES FOLDER "Benchmarks")
