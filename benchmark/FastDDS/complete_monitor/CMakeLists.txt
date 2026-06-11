cmake_minimum_required(VERSION 3.16)
project(fastdds_v2x_monitor LANGUAGES CXX)

find_package(fastdds REQUIRED)
find_package(fastcdr REQUIRED)
# NUEVO: Buscamos la librería del backend de estadísticas
find_package(fastdds_statistics_backend REQUIRED)

set(CMAKE_CXX_STANDARD 17)

# Archivos generados por fastddsgen
set(IDL_SOURCES
    vehiclePubSubTypes.cxx
    vehicleTypeObjectSupport.cxx
)

# 1. Compilar el nodo de los vehículos
add_executable(vehicle_node
    vehicle_node.cpp
    ${IDL_SOURCES}
)
target_link_libraries(vehicle_node fastdds fastcdr)

# 2. NUEVO: Compilar el monitor de estadísticas
add_executable(monitor monitor.cpp)
target_link_libraries(monitor fastdds_statistics_backend)
