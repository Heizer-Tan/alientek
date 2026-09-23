cmake_minimum_required(VERSION 3.16)
project(sensor-dashboard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(sensor-dashboard
	main.cpp
	dashboard.cpp
	sensors.cpp
	iio-icm.c
)
target_link_libraries(sensor-dashboard PRIVATE Qt6::Widgets m)

install(TARGETS sensor-dashboard RUNTIME DESTINATION bin)
