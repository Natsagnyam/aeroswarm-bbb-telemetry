cmake_minimum_required(VERSION 3.12)
project(AeroSwarmTelemetry CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(flight_engine src/main.cpp)

if(UNIX AND NOT APPLE)
    target_link_libraries(flight_engine pthread)
endif()
