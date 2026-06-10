cmake_minimum_required(VERSION 3.15)
project(DarlingQuartzCore LANGUAGES OBJC CXX)

set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

add_library(DarlingQuartzCore SHARED
    CAMetalLayer.m
    QuartzCoreClasses.m
)

target_include_directories(DarlingQuartzCore PUBLIC
    .
    ../DarlingMetal/include
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(DarlingQuartzCore PRIVATE
    ${Vulkan_LIBRARIES}
)

if(APPLE)
    target_link_options(DarlingQuartzCore PRIVATE "-framework" "Foundation")
else()
    target_compile_options(DarlingQuartzCore PRIVATE "-fobjc-runtime=gnustep" "-fblocks")
    target_link_libraries(DarlingQuartzCore PRIVATE objc)
endif()
