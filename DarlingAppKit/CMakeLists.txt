cmake_minimum_required(VERSION 3.15)
project(DarlingAppKit LANGUAGES C)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-runtime=gnustep -fblocks -fno-objc-arc -fPIC")

include_directories(
    /usr/include/GNUstep
)

add_library(AppKit SHARED
    AppKitClasses.m
)

set_source_files_properties(AppKitClasses.m PROPERTIES COMPILE_FLAGS "-x objective-c")

target_link_libraries(AppKit PRIVATE objc)

set_target_properties(AppKit PROPERTIES 
    OUTPUT_NAME "AppKit"
    PREFIX ""
    SUFFIX ".so"
)
