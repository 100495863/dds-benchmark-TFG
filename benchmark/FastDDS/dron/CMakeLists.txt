cmake_minimum_required(VERSION 3.16)
project(fastdds_dron LANGUAGES CXX)

find_package(fastdds REQUIRED)
find_package(fastcdr REQUIRED)

set(CMAKE_CXX_STANDARD 11)

set(IDL_SOURCES
    dronPubSubTypes.cxx
    dronTypeObjectSupport.cxx
)

add_executable(sensor_imu
    sensor_imu.cpp
    ${IDL_SOURCES}
)
target_link_libraries(sensor_imu fastdds fastcdr)

add_executable(sensor_env
    sensor_env.cpp
    ${IDL_SOURCES}
)
target_link_libraries(sensor_env fastdds fastcdr)

add_executable(flight_controller
    flight_controller.cpp
    ${IDL_SOURCES}
)
target_link_libraries(flight_controller fastdds fastcdr)
