cmake_minimum_required (VERSION 3.1)

project(MIMUW_SIK_ZAD1)

set(CMAKE_C_STANDARD 20)
# set(CMAKE_CXX_FLAGS "-g -Wall -Wextra -O2") # Release
set(CMAKE_CXX_FLAGS "-g -Wall -Wextra") # Debug

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.45.0 COMPONENTS program_options)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    add_executable(sikradio-sender sikradio-sender.cpp net_utils.h utils.cpp utils.h ui.cpp ui.h)
    add_executable(sikradio-receiver sikradio-receiver.cpp net_utils.h utils.cpp utils.h ui.cpp ui.h)
    target_link_libraries(sikradio-sender ${Boost_LIBRARIES})
    target_link_libraries(sikradio-receiver ${Boost_LIBRARIES})
endif()

install(TARGETS DESTINATION .)
