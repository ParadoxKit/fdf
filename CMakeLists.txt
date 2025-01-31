cmake_minimum_required(VERSION 3.31)


if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


option(FDF_USE_CPP_MODULES "Enable C++ modules" OFF)


project(fdf VERSION 0.1.0 LANGUAGES CXX)
add_library(fdf STATIC)
target_compile_features(fdf PUBLIC cxx_std_23)
target_include_directories(fdf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_sources(fdf
    PRIVATE
        src/fdf.cpp
        include/fdf.h
)


target_compile_definitions(fdf PUBLIC FDF_ROOT_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(fdf PUBLIC FDF_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/tests")


if(FDF_USE_CPP_MODULES)
    target_sources(fdf
        PRIVATE
            FILE_SET my_modules
            TYPE CXX_MODULES
            FILES src/fdf.ixx
    )
    target_compile_definitions(fdf PUBLIC FDF_USE_CPP_MODULES)
endif()


if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(tests)
endif()
