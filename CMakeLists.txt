cmake_minimum_required(VERSION 3.14)
project(media_scanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <filesystem>\nint main() { std::filesystem::path p; }" FILESYSTEM_NO_LINK)

if(NOT FILESYSTEM_NO_LINK)
    set(CMAKE_REQUIRED_LIBRARIES stdc++fs)
    check_cxx_source_compiles("#include <filesystem>\nint main() { std::filesystem::path p; }" FILESYSTEM_NEED_LINK)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(FILESYSTEM_NEED_LINK)
        link_libraries(stdc++fs)
    endif()
endif()

add_executable(media_scanner main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(media_scanner PRIVATE Threads::Threads)

install(TARGETS media_scanner DESTINATION bin)
