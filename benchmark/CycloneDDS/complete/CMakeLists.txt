cmake_minimum_required(VERSION 3.16)
project(cyclonedds_vehicle LANGUAGES C)

find_package(CycloneDDS REQUIRED)

# Generamos el código C a partir de vehicle.idl
idlc_generate(
        TARGET VehicleData_lib
        FILES "vehicle.idl"
        WARNINGS no-implicit-extensibility
)

# Creamos el ejecutable apuntando a vehicle_node.c
add_executable(vehicle_node vehicle_node.c)
target_link_libraries(vehicle_node VehicleData_lib CycloneDDS::ddsc m)
