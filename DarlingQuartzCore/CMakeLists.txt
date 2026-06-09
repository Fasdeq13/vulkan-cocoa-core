cmake_minimum_required(VERSION 3.15)
project(DarlingQuartzCore LANGUAGES C)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-runtime=gnustep -fblocks -fno-objc-arc -fPIC")

include_directories(
    /usr/include/GNUstep
)

add_library(QuartzCore SHARED
    CAMetalLayer.m
    QuartzCoreClasses.m
)

set_source_files_properties(CAMetalLayer.m PROPERTIES COMPILE_FLAGS "-x objective-c")
set_source_files_properties(QuartzCoreClasses.m PROPERTIES COMPILE_FLAGS "-x objective-c")

target_link_libraries(QuartzCore PRIVATE objc)

set_target_properties(QuartzCore PROPERTIES 
    OUTPUT_NAME "QuartzCore"
    PREFIX ""
    SUFFIX ".so"
)
