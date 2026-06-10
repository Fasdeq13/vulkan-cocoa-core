cmake_minimum_required(VERSION 3.15)
project(DarlingMetal LANGUAGES OBJC CXX)

set(CMAKE_OBJC_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

add_library(DarlingMetal SHARED
    src/MTLDevice.m
    src/MTLClasses.m
)

target_include_directories(DarlingMetal PUBLIC
    include
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(DarlingMetal PRIVATE
    ${Vulkan_LIBRARIES}
)

if(APPLE)
    target_link_options(DarlingMetal PRIVATE "-framework" "Foundation")
else()
    target_compile_options(DarlingMetal PRIVATE "-fobjc-runtime=gnustep" "-fblocks")
    target_link_libraries(DarlingMetal PRIVATE objc)
endif()
