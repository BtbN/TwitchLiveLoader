cmake_minimum_required(VERSION 2.8.12)

project(twliveload)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

if(NOT MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
set(CMAKE_AUTOMOC TRUE)

find_package(Qt5Core REQUIRED)
find_package(Qt5Network REQUIRED)

add_executable(twliveload
	main.cpp
	twitchhandler.cpp
	twitchhandler.h)
target_link_libraries(twliveload
	Qt5::Core
	Qt5::Network)
