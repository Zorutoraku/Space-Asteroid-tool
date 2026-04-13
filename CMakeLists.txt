cmake_minimum_required(VERSION 3.20)
project(AsteroidMiner VERSION 0.9.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# ncurses
find_package(Curses REQUIRED)

add_executable(asteroid_miner
    src/main.cpp
    src/api_client.cpp
    src/asteroid.cpp
    src/calculator.cpp
    src/mission.cpp
    src/database.cpp
    src/tui.cpp
)

target_include_directories(asteroid_miner PRIVATE src ${CURSES_INCLUDE_DIR})
target_link_libraries(asteroid_miner PRIVATE
    CURL::libcurl
    SQLite3::SQLite3
    ${CURSES_LIBRARIES}
)
