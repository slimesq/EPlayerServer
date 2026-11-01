cmake_minimum_required(VERSION 3.22)
project(EPlayerServer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
set(EPLAYER_MYSQL_ROOT "" CACHE PATH "MySQL client installation prefix")
set(_mysql_hints "${EPLAYER_MYSQL_ROOT}" "$ENV{HOME}/.local/opt/eplayer-mysql-deps/usr")
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h HINTS ${_mysql_hints} PATH_SUFFIXES include)
find_library(MYSQL_LIBRARY NAMES mysqlclient HINTS ${_mysql_hints}
    PATH_SUFFIXES lib lib/x86_64-linux-gnu lib/aarch64-linux-gnu)
if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "MySQL client development files are required. On Ubuntu: sudo apt install libmysqlclient-dev; alternatively set EPLAYER_MYSQL_ROOT.")
endif()
add_library(eplayer_mysql UNKNOWN IMPORTED)
set_target_properties(eplayer_mysql PROPERTIES IMPORTED_LOCATION "${MYSQL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MYSQL_INCLUDE_DIR}")

add_library(eplayer_http STATIC third_party/http_parser/http_parser.c)
target_include_directories(eplayer_http PUBLIC third_party/http_parser)
add_library(eplayer_sqlite STATIC third_party/sqlite3/sqlite3.c)
target_include_directories(eplayer_sqlite PUBLIC third_party)
target_link_libraries(eplayer_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
add_library(eplayer_json STATIC
    third_party/jsoncpp/json_reader.cpp
    third_party/jsoncpp/json_value.cpp
    third_party/jsoncpp/json_writer.cpp)
target_include_directories(eplayer_json PUBLIC third_party)

file(GLOB EPLAYER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM EPLAYER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(eplayer_core STATIC ${EPLAYER_SOURCES})
target_include_directories(eplayer_core PUBLIC inc)
target_compile_definitions(eplayer_core PUBLIC _GNU_SOURCE $<$<CONFIG:Debug>:_DEBUG>)
target_link_libraries(eplayer_core PUBLIC Threads::Threads OpenSSL::Crypto
    eplayer_mysql eplayer_http eplayer_sqlite eplayer_json)

add_executable(EPlayerServer src/main.cpp)
target_link_libraries(EPlayerServer PRIVATE eplayer_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(eplayer_tests tests/components.cpp)
    target_link_libraries(eplayer_tests PRIVATE eplayer_core)
    add_test(NAME eplayer_components COMMAND eplayer_tests)
    set_tests_properties(eplayer_components PROPERTIES TIMEOUT 30)
endif()
