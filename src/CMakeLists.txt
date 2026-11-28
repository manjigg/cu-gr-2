#########
# Setup #
#########

# Specify the minimum version for CMake
cmake_minimum_required(VERSION 3.6)

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
option(CUGR_NATIVE_3D "Build the POSIX native dual-die importer and router" ON)
option(CUGR_DP_MEMORY_PROFILE "Instrument live fast-DP label payload for diagnostic runs" OFF)
if(CUGR_NATIVE_3D AND CUGR_DP_MEMORY_PROFILE)
    add_definitions(-DCUGR_DP_MEMORY_PROFILE)
endif()
set(BOOST_COMPONENTS system filesystem program_options)
if(CUGR_NATIVE_3D)
    list(APPEND BOOST_COMPONENTS serialization)
    add_definitions(-DCUGR_NATIVE_3D -DCUGR_LUT_DIR="${PATH_FLUTE}")
endif()
find_package(Boost COMPONENTS ${BOOST_COMPONENTS} REQUIRED)
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

if(NOT CUGR_NATIVE_3D)
    list(FILTER SRC_FILES_CUGR EXCLUDE REGEX "/native/")
endif()
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

list(REMOVE_ITEM SRC_FILES ${PATH_CUGR}/main.cpp)
add_library(cugr_objects OBJECT ${SRC_FILES})
add_executable(route main.cpp $<TARGET_OBJECTS:cugr_objects>)

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
option(CUGR_STATIC_ROUTE "Statically link the router executable" ON)
if(CUGR_STATIC_ROUTE)
    set_target_properties(route PROPERTIES LINK_FLAGS "-static -Wl,--whole-archive -rdynamic -lpthread -Wl,--no-whole-archive")
else()
    target_link_libraries(route pthread)
endif()

# LEF/DEF
target_link_libraries(route lef)
target_link_libraries(route def)

# Boost
target_include_directories(route PUBLIC ${Boost_INCLUDE_DIR})
target_link_libraries(route ${Boost_LIBRARIES})

if(CUGR_NATIVE_3D)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(native_replay ../tests/native/replay.cpp $<TARGET_OBJECTS:cugr_objects>)
        target_link_libraries(native_replay lef def ${Boost_LIBRARIES} pthread)
        add_executable(native_resources_test ../tests/native/resources.cpp $<TARGET_OBJECTS:cugr_objects>)
        target_link_libraries(native_resources_test lef def ${Boost_LIBRARIES} pthread)
        add_test(NAME native_resources COMMAND native_resources_test ${PATH_CUGR}/../tests/native/dual.json)
        add_test(NAME native_import COMMAND python3 ${PATH_CUGR}/../tests/native/test_import.py $<TARGET_FILE:route>)
    endif()
endif()

if(CUGR_NATIVE_3D AND BUILD_TESTING)
    add_executable(native_worker_pool_test ../tests/native/worker_pool.cpp native/WorkerPool.cpp)
    target_link_libraries(native_worker_pool_test pthread)
    add_test(NAME native_worker_pool COMMAND native_worker_pool_test)
    set_tests_properties(native_worker_pool PROPERTIES TIMEOUT 20)
    add_executable(native_resource_view_test ../tests/native/resource_view.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_resource_view_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_resource_view COMMAND native_resource_view_test ${PATH_CUGR}/../tests/native/dual.json)
    add_executable(native_hbt_footprint_test ../tests/native/hbt_footprint.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_hbt_footprint_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_hbt_footprint COMMAND native_hbt_footprint_test ${PATH_CUGR}/../tests/native/dual.json)
    add_executable(native_topology_test ../tests/native/topology_refinement.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_topology_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_topology COMMAND python3 ${PATH_CUGR}/../tests/native/test_topology.py $<TARGET_FILE:native_topology_test>)
    add_executable(native_incremental_dp_test ../tests/native/incremental_dp.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_incremental_dp_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_incremental_dp COMMAND native_incremental_dp_test ${PATH_CUGR}/../tests/native/clusters/manifest.json)
    add_executable(native_pattern_test ../tests/native/pattern.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_pattern_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_parallel_maze COMMAND python3 ${PATH_CUGR}/../tests/native/test_parallel_maze.py $<TARGET_FILE:route>)
    add_test(NAME native_hbt_budget COMMAND python3 ${PATH_CUGR}/../tests/native/test_budget.py $<TARGET_FILE:route>)
    add_test(NAME native_maze_checkpoint COMMAND python3 ${PATH_CUGR}/../tests/native/test_maze_checkpoint.py $<TARGET_FILE:route>)
    add_executable(native_conflict_repair_test ../tests/native/conflict_repair.cpp $<TARGET_OBJECTS:cugr_objects>)
    target_link_libraries(native_conflict_repair_test lef def ${Boost_LIBRARIES} pthread)
    add_test(NAME native_conflict_repair COMMAND python3 ${PATH_CUGR}/../tests/native/test_conflict_repair.py $<TARGET_FILE:native_conflict_repair_test>)
    add_test(NAME native_flow COMMAND python3 ${PATH_CUGR}/../tests/native/test_flow.py $<TARGET_FILE:route>)
    add_test(NAME native_pattern COMMAND native_pattern_test ${PATH_CUGR}/../tests/native/dual.json)
    foreach(table POWV9.dat POST9.dat)
        configure_file(${PATH_FLUTE}/${table} ${CMAKE_BINARY_DIR}/${table} COPYONLY)
    endforeach()
endif()
