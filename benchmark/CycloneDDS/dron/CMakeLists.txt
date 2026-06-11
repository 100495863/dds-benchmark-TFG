cmake_minimum_required(VERSION 3.16)
project(cyclonedds_dron LANGUAGES C)

find_package(CycloneDDS REQUIRED)

idlc_generate(
        TARGET IcarusData_lib
        FILES "dron.idl"
        WARNINGS no-implicit-extensibility
)

add_executable(sensor_imu sensor_imu.c)
target_link_libraries(sensor_imu IcarusData_lib CycloneDDS::ddsc m)

add_executable(sensor_env sensor_env.c)
target_link_libraries(sensor_env IcarusData_lib CycloneDDS::ddsc m)

add_executable(flight_controller flight_controller.c)
target_link_libraries(flight_controller IcarusData_lib CycloneDDS::ddsc m)
