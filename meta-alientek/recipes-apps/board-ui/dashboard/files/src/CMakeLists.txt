cmake_minimum_required(VERSION 3.16)
project(dashboard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} hw)

add_executable(dashboard
	main.cpp
	ui/dashboard/dashboard.cpp
	ui/style/pixel_widgets.cpp
	hw/sensors/sensors.cpp
	hw/leds/leds.cpp
	sys/sysinfo/sysinfo.cpp
	sys/ota/ota_status.cpp
	hw/keys/keys.cpp
	hw/iio-icm.c
	hw/input-device.c
)
target_link_libraries(dashboard PRIVATE Qt6::Widgets m)

install(TARGETS dashboard RUNTIME DESTINATION bin)
