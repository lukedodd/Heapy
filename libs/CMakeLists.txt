
file(GLOB MinHookSources "MinHook/src/*.h" "MinHook/src/*.cpp" "MinHook/src/HDE64/include/*.h" "MinHook/src/HDE64/src/*.c"  "MinHook/src/HDE64/src/*.h")
include_directories(MinHook/include)
add_library(MinHook ${MinHookSources})
