
cmake_minimum_required(VERSION 3.27)

project(vRenderer)
set(project vRenderer)
set(folder "vRenderer")
set(source_folder "source/")
set(shaders_folder "shaders/")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)


if (MSVC)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_ITERATOR_DEBUG_LEVEL=1")
    # Disable C++ exceptions.
    string(REGEX REPLACE "/EH[a-z]+" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

option(DONUT_WITH_ASSIMP "" OFF)
option(DONUT_WITH_DX11 "" OFF)
option(DONUT_WITH_VULKAN "" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/_bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/_shaders/donut")

add_definitions(-DIMGUI_DEFINE_MATH_OPERATORS)

if (NOT TARGET ShaderMake)
	set(SHADERMAKE_BIN_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/_bin" CACHE STRING "" FORCE)
endif()


add_subdirectory(donut)

##########################

include(donut/compileshaders.cmake)
file(GLOB shaders "*.hlsl")
file(GLOB sources "${source_folder}*.cpp" "${source_folder}/*/*.cpp" "${source_folder}*.h" "${source_folder}/*/*.h" "${source_folder}/*/*/*.cpp" "${source_folder}/*/*/*.h")

donut_compile_shaders_all_platforms(
    TARGET ${project}_shaders
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/${shaders_folder}/shaders.cfg
    FOLDER ${folder}
    OUTPUT_BASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/_Shaders/${project}
)

add_executable(${project} WIN32 ${sources})
target_link_libraries(${project} PUBLIC donut_render donut_app donut_engine PRIVATE ws2_32)
add_dependencies(${project} ${project}_shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()