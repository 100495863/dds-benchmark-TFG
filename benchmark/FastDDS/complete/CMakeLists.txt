cmake_minimum_required(VERSION 3.16)
project(fastdds_v2x_complete LANGUAGES CXX)

find_package(fastdds REQUIRED)
find_package(fastcdr REQUIRED)

set(CMAKE_CXX_STANDARD 11)

# Archivos generados por fastddsgen
set(IDL_SOURCES
    vehiclePubSubTypes.cxx
    vehicleTypeObjectSupport.cxx
)

add_executable(vehicle_node
    vehicle_node.cpp
    ${IDL_SOURCES}
)

target_link_libraries(vehicle_node fastdds fastcdr)
