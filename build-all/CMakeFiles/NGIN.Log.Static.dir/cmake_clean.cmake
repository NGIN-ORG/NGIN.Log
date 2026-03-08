file(REMOVE_RECURSE
  "libNGINLog.a"
  "libNGINLog.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/NGIN.Log.Static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
