cmake_minimum_required(VERSION 3.15)
project(DarlingMetalPaint LANGUAGES C)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-runtime=gcc -fblocks -fno-objc-arc -fPIC")

include_directories(/usr/include/GNUstep)

find_package(Vulkan REQUIRED)
find_library(OBJC_LIB NAMES objc)
find_library(GNUSTEP_BASE_LIB NAMES gnustep-base)
find_library(X11_LIB NAMES X11)

add_executable(test_paint test_paint.m)
set_source_files_properties(test_paint.m PROPERTIES COMPILE_FLAGS "-x objective-c")

target_link_libraries(test_paint PRIVATE 
    Vulkan::Vulkan
    ${OBJC_LIB}
    ${GNUSTEP_BASE_LIB}
    ${X11_LIB}
)
