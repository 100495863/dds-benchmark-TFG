cmake_minimum_required(VERSION 3.16)
project(cyclonedds_benchmark LANGUAGES C)

find_package(CycloneDDS REQUIRED)

idlc_generate(
        TARGET BenchmarkData_lib
        FILES "benchmark_mes.idl"
        WARNINGS no-implicit-extensibility
)

add_executable(publisher publisher.c)
add_executable(subscriber subscriber.c)

target_link_libraries(publisher BenchmarkData_lib CycloneDDS::ddsc)
target_link_libraries(subscriber BenchmarkData_lib CycloneDDS::ddsc)
