file(REMOVE_RECURSE
  "libNGINBase.a"
  "libNGINBase.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/NGIN.Base.Static.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
