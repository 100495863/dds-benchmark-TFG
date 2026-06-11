cmake_minimum_required(VERSION 3.16)
project(fastdds_streaming LANGUAGES CXX)

find_package(fastdds REQUIRED)
find_package(fastcdr REQUIRED)

set(CMAKE_CXX_STANDARD 11)

# Archivos generados automáticamente por fastddsgen
set(IDL_SOURCES
    multimediaPubSubTypes.cxx
    multimediaTypeObjectSupport.cxx
)

add_executable(streaming_node
    streaming_node.cpp
    ${IDL_SOURCES}
)

target_link_libraries(streaming_node fastdds fastcdr pthread)
