cmake_minimum_required(VERSION 2.8)
project(Heapy) 

add_subdirectory(libs)

include_directories(${CMAKE_SOURCE_DIR}/libs/MinHook/include ${CMAKE_SOURCE_DIR}/libs/dbghelp/include)
link_directories(${CMAKE_SOURCE_DIR}/libs/dbghelp/lib/x64)

add_library(HeapyInjectDll SHARED HeapyInject/HeapyInject.cpp HeapyInject/HeapProfiler.cpp)
target_link_libraries(HeapyInjectDll MinHook dbghelp.lib)

add_executable(Heapy Heapy/Heapy.cpp)
target_link_libraries(Heapy)
