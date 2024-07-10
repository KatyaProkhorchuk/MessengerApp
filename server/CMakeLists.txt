cmake_minimum_required(VERSION 3.24)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# include_directories(-I/usr/local/include)
# link_directories(-L/usr/local/lib)
find_package(Boost 1.76 REQUIRED COMPONENTS system)

add_executable(chat_server  main.cpp)

# if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    target_link_libraries(chat_server ${Boost_LIBRARIES})
# endif()