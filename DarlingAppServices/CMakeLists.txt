cmake_minimum_required(VERSION 3.15)
project(DarlingAppServices LANGUAGES OBJC CXX)

set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_library(DarlingAppServices SHARED
    AppServicesClasses.m
)

target_include_directories(DarlingAppServices PUBLIC
    .
)

if(APPLE)
    target_link_options(DarlingAppServices PRIVATE "-framework" "Foundation")
else()
    target_compile_options(DarlingAppServices PRIVATE "-fobjc-runtime=gnustep" "-fblocks")
    target_link_libraries(DarlingAppServices PRIVATE objc)
endif()
