cmake_minimum_required(VERSION 3.16)


project(fastdds_latency LANGUAGES CXX)


find_package(fastdds REQUIRED)
find_package(fastcdr REQUIRED)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(IDL_SOURCES
benchmarkPubSubTypes.cxx
benchmarkTypeObjectSupport.cxx
)


add_executable(publisher
publisher.cpp
${IDL_SOURCES}
)


target_link_libraries(publisher fastdds fastcdr)


add_executable(subscriber
subscriber.cpp
${IDL_SOURCES}
)


target_link_libraries(subscriber fastdds fastcdr)
