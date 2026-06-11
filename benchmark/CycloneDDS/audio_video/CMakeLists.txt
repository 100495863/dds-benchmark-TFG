cmake_minimum_required(VERSION 3.16)
project(cyclonedds_streaming LANGUAGES C)

find_package(CycloneDDS REQUIRED)

# Generar el código C a partir de multimedia.idl al vuelo
idlc_generate(
    TARGET MultimediaData_lib
    FILES "multimedia.idl"
    WARNINGS no-implicit-extensibility
)

# Compilar el nodo principal
add_executable(streaming_node streaming_node.c)
target_link_libraries(streaming_node MultimediaData_lib CycloneDDS::ddsc pthread m)
