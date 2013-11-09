cmake_minimum_required(VERSION 2.8)
project(Heapy) 

add_subdirectory(libs)

include_directories(${CMAKE_SOURCE_DIR}/libs/MinHook/include ${CMAKE_SOURCE_DIR}/libs/dbghelp/include)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	link_directories(${CMAKE_SOURCE_DIR}/libs/dbghelp/lib/x64)
else(CMAKE_SIZEOF_VOID_P EQUAL 8)
	link_directories(${CMAKE_SOURCE_DIR}/libs/dbghelp/lib/x86)
endif(CMAKE_SIZEOF_VOID_P EQUAL 8)


add_library(HeapyInjectDll SHARED HeapyInject/HeapyInject.cpp HeapyInject/HeapProfiler.cpp)
target_link_libraries(HeapyInjectDll MinHook dbghelp.lib)

add_executable(Heapy Heapy/Heapy.cpp)
target_link_libraries(Heapy)

# Heapy depends on HeapyInjectDll even though we don't actually link to it.
ADD_DEPENDENCIES(Heapy HeapyInjectDll)

# Copy dbghelp dll to output directory.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
add_custom_command(TARGET Heapy POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${PROJECT_SOURCE_DIR}/libs/dbghelp/lib/x64/dbghelp.dll" 
	$<TARGET_FILE_DIR:Heapy>) 
else(CMAKE_SIZEOF_VOID_P EQUAL 8)
add_custom_command(TARGET Heapy POST_BUILD        
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  
        "${PROJECT_SOURCE_DIR}/libs/dbghelp/lib/x86/dbghelp.dll" 
	$<TARGET_FILE_DIR:Heapy>) 
endif(CMAKE_SIZEOF_VOID_P EQUAL 8)

