cmake_minimum_required(VERSION 3.16)

project(ltool
    VERSION 1.0.0
    DESCRIPTION "Header-only C++ utility library"
    LANGUAGES CXX
)

include(GNUInstallDirs)

option(LTOOL_USE_EXTERNAL_FMT "Use an external fmt package instead of bundled headers" OFF)
option(LTOOL_USE_MAGIC_ENUM "Enable magic_enum support in lstring" ON)
option(LTOOL_USE_RFL_JSON "Enable reflect-cpp rfl/json integration when available" ON)
option(LTOOL_USE_NLOHMANN_JSON "Enable nlohmann/json integration when available" ON)
option(LTOOL_USE_JSONCPP "Enable JsonCpp integration when available" ON)
option(LTOOL_USE_SIMDJSON "Enable simdjson integration when available" ON)
option(LTOOL_USE_YYJSON "Enable yyjson integration when available" ON)

add_library(ltool INTERFACE)
add_library(ltool::ltool ALIAS ltool)

target_compile_features(ltool INTERFACE cxx_std_11)

target_include_directories(ltool
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${PROJECT_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/ltool>
)

if(LTOOL_USE_EXTERNAL_FMT)
    find_package(fmt CONFIG REQUIRED)
    target_compile_definitions(ltool INTERFACE LTOOL_USE_EXTERNAL_FMT=1 LSTRING_USE_EXTERNAL_FMT=1)
    target_link_libraries(ltool INTERFACE fmt::fmt)
endif()

if(NOT LTOOL_USE_MAGIC_ENUM)
    target_compile_definitions(ltool INTERFACE LTOOL_USE_MAGIC_ENUM=0 LSTRING_USE_MAGIC_ENUM=0)
endif()

if(LTOOL_USE_RFL_JSON)
    target_compile_definitions(ltool INTERFACE LTOOL_USE_RFL_JSON=1)
else()
    target_compile_definitions(ltool INTERFACE LTOOL_USE_RFL_JSON=0)
endif()

if(LTOOL_USE_NLOHMANN_JSON)
    find_package(nlohmann_json CONFIG QUIET)
    if(TARGET nlohmann_json::nlohmann_json)
        target_link_libraries(ltool INTERFACE nlohmann_json::nlohmann_json)
    endif()
else()
    target_compile_definitions(ltool INTERFACE LTOOL_USE_NLOHMANN_JSON=0)
endif()

if(LTOOL_USE_JSONCPP)
    find_package(jsoncpp CONFIG QUIET)
    if(TARGET JsonCpp::JsonCpp)
        target_link_libraries(ltool INTERFACE JsonCpp::JsonCpp)
    elseif(TARGET jsoncpp_lib)
        target_link_libraries(ltool INTERFACE jsoncpp_lib)
    endif()
else()
    target_compile_definitions(ltool INTERFACE LTOOL_USE_JSONCPP=0 LTOOL_USE_CPPJSON=0)
endif()

if(LTOOL_USE_SIMDJSON)
    find_package(simdjson CONFIG QUIET)
    if(TARGET simdjson::simdjson)
        target_link_libraries(ltool INTERFACE simdjson::simdjson)
    endif()
else()
    target_compile_definitions(ltool INTERFACE LTOOL_USE_SIMDJSON=0)
endif()

if(NOT LTOOL_USE_YYJSON)
    target_compile_definitions(ltool INTERFACE LTOOL_USE_YYJSON=0)
endif()

if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    find_library(LTOOL_ICONV_LIBRARY iconv)
    if(LTOOL_ICONV_LIBRARY)
        target_link_libraries(ltool INTERFACE ${LTOOL_ICONV_LIBRARY})
    endif()
endif()

install(
    DIRECTORY ${PROJECT_CURRENT_SOURCE_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ltool
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    TARGETS ltool
    EXPORT ltoolTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT ltoolTargets
    NAMESPACE ltool::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ltool
)
