# 文件功能：配置 C++14 route 可执行文件，汇总 CUGR、Rsyn 和 FLUTE 源码并链接依赖。
# 构建路径：收集源文件 -> 设置头文件路径 -> 链接静态 LEF/DEF、Boost 和线程库。
# 【白话职责】这是“怎样把源码做成可执行程序”的配方，不是运行时的布线步骤。
# CMake 先读配方生成构建规则，编译器将各 .cpp/.c 编成目标文件，链接器再拼成 route。
# set 给变量赋值，file(GLOB_RECURSE ...) 收集目录里的源文件，include_directories 告诉
# 编译器去哪里找 #include 的头文件，target_link_libraries 告诉链接器需要哪些库。
# .h 通常由 #include 带入编译，不需要逐个列成独立可执行源文件。
# PATH_RSYN/PATH_FLUTE/PATH_CUGR 是三部分源码的位置，最后编进同一个 route。
#########
# Setup #
#########

# Specify the minimum version for CMake
cmake_minimum_required(VERSION 2.8)

# Message
message(STATUS "Global Router CUGR 2.0")
message(STATUS ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS ${PROJECT_SOURCE_DIR})

# Find includes in the build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Project's name
project(CUGR)

# Set the output folder where your program will be created
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

set(CMAKE_CXX_STANDARD 14)
# CMAKE_CXX_STANDARD 指 C++ 语言版本，不是线程数；PATH_* 为三个源码根目录。
# SRC_FILES_* 是源文件清单，SRC_FILES 是合并清单；Boost_INCLUDE_DIR/LIBRARIES 为依赖搜索结果。
# CMAKE_BINARY_DIR 输出构建目录，EXECUTABLE_OUTPUT_PATH 最终程序位置，不是 guide 输出路径。

set(PATH_RSYN ${CMAKE_CURRENT_SOURCE_DIR}/../rsyn)
set(PATH_FLUTE ${CMAKE_CURRENT_SOURCE_DIR}/../flute)
set(PATH_CUGR ${CMAKE_CURRENT_SOURCE_DIR})

###################
# Warnings/Errors #
###################

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Werror=return-type")

######################
# Check Dependencies #
######################
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS system filesystem program_options REQUIRED)
# find_package(Threads)

###############
# Source Code #
###############
file(GLOB_RECURSE SRC_FILES_RSYN
    ${PATH_RSYN}/src/*.cpp
    ${PATH_RSYN}/src/*.cc
    ${PATH_RSYN}/src/*.c)
    
file(GLOB_RECURSE SRC_FILES_FLUTE ${PATH_FLUTE}/*.c)

file(GLOB_RECURSE SRC_FILES_CUGR
    ${PATH_CUGR}/*.cpp
    ${PATH_CUGR}/*.c)

set(SRC_FILES ${SRC_FILES_RSYN} ${SRC_FILES_FLUTE} ${SRC_FILES_CUGR})

#################
# Library Paths #
#################
# Need to come before target is created.

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
link_directories(${PATH_RSYN}/lib/linux)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
link_directories(${PATH_RSYN}/lib/macosx)
endif()

###########
# Targets #
###########

add_executable(route ${SRC_FILES})

#######################
# Include Directories #
#######################

include_directories(${PATH_RSYN}/src)
include_directories(${PATH_RSYN}/src/rsyn/export)
include_directories(${PATH_RSYN}/include)
include_directories(${PATH_FLUTE})
include_directories(${PATH_CUGR})

#################
# Linker Flags  #
#################

# TODO: set static under release mode only
set_target_properties(route PROPERTIES LINK_FLAGS "-static -Wl,--whole-archive -rdynamic -lpthread -Wl,--no-whole-archive")

# LEF/DEF
target_link_libraries(route lef)
target_link_libraries(route def)

# Boost
target_include_directories(route PUBLIC ${Boost_INCLUDE_DIR})
target_link_libraries(route ${Boost_LIBRARIES})

# H3D 小规模精确对照与完整双 die 回归，不需要真实 benchmark 或 Innovus。
option(CUGR_BUILD_TESTS "Build H3D algorithm and end-to-end checks" OFF)
if(CUGR_BUILD_TESTS)
    enable_testing()
    add_executable(h3d_die_assignment_test
        ../tests/h3d/DieAssignmentTest.cpp h3d/H3DTree.cpp h3d/DieAssignment.cpp)
    add_executable(h3d_hbt_optimization_test
        ../tests/h3d/HbtOptimizationTest.cpp h3d/H3DTree.cpp h3d/DieAssignment.cpp
        h3d/HbtGrid.cpp h3d/HbtLocationOptimizer.cpp)
    add_executable(h3d_location_replay
        ../tests/h3d/ReplayLocationTest.cpp h3d/H3DTree.cpp h3d/DieAssignment.cpp
        h3d/HbtGrid.cpp h3d/HbtLocationOptimizer.cpp)
    add_executable(h3d_paper_method_test
        ../tests/h3d/PaperMethodTest.cpp h3d/H3DTree.cpp h3d/DieAssignment.cpp
        h3d/HbtGrid.cpp h3d/HbtLocationOptimizer.cpp h3d/SubnetPartition.cpp)
    add_test(NAME h3d_die_assignment COMMAND h3d_die_assignment_test)
    add_test(NAME h3d_hbt_optimization COMMAND h3d_hbt_optimization_test)
    add_test(NAME h3d_paper_method COMMAND h3d_paper_method_test)
    find_package(PythonInterp 3 QUIET)
    if(PYTHONINTERP_FOUND)
        add_test(NAME h3d_input_provenance COMMAND ${PYTHON_EXECUTABLE}
            ${PATH_CUGR}/../tests/h3d/check_input_provenance.py)
        add_test(NAME single_die_fixture COMMAND ${PYTHON_EXECUTABLE}
            ${PATH_CUGR}/../tests/h3d/check_fixture.py --route $<TARGET_FILE:route>
            --output-dir ${CMAKE_BINARY_DIR}/h3d_ctest_smoke)
        add_test(NAME h3d_end_to_end COMMAND ${PYTHON_EXECUTABLE}
            ${PATH_CUGR}/../tests/h3d/check_end_to_end.py --route $<TARGET_FILE:route>
            --output-dir ${CMAKE_BINARY_DIR}/h3d_ctest_e2e)
    endif()
endif()
