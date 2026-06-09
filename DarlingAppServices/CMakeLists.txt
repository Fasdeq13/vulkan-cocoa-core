cmake_minimum_required(VERSION 3.15)
project(DarlingAppServices LANGUAGES C)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-runtime=gnustep -fblocks -fno-objc-arc -fPIC")

include_directories(
    /usr/include/GNUstep
)

add_library(ApplicationServices SHARED
    AppServicesClasses.m
)

set_source_files_properties(AppServicesClasses.m PROPERTIES COMPILE_FLAGS "-x objective-c")

target_link_libraries(ApplicationServices PRIVATE objc)

set_target_properties(ApplicationServices PROPERTIES 
    OUTPUT_NAME "ApplicationServices"
    PREFIX ""
    SUFFIX ".so"
)
