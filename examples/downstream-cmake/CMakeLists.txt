cmake_minimum_required (VERSION 2.8.12)
project(jokertv_example)
set(CMAKE_BUILD_TYPE Release)
find_package(jokertv 0.9.0 REQUIRED)

add_executable(jokertv_example main.c)
target_link_libraries(jokertv_example jokertv)
install(TARGETS jokertv_example DESTINATION bin)
