

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	file(GLOB MinHookSources "MinHook/src/*.h" "MinHook/src/*.cpp" "MinHook/src/HDE64/include/*.h" "MinHook/src/HDE64/src/*.c"  "MinHook/src/HDE64/src/*.h")
else(CMAKE_SIZEOF_VOID_P EQUAL 8)
	file(GLOB MinHookSources "MinHook/src/*.h" "MinHook/src/*.cpp" "MinHook/src/HDE32/*.h" "MinHook/src/HDE32/*.c")
endif(CMAKE_SIZEOF_VOID_P EQUAL 8)



include_directories(MinHook/include)
add_library(MinHook ${MinHookSources})
