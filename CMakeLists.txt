cmake_minimum_required(VERSION 3.16)
project(M1FixedPoint LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DATE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/date"
    CACHE PATH "Path to Howard Hinnant date source (tested with v3.0.3)")
if(NOT EXISTS "${DATE_SOURCE_DIR}/CMakeLists.txt"
   OR NOT EXISTS "${DATE_SOURCE_DIR}/include/date/tz.h"
   OR NOT EXISTS "${DATE_SOURCE_DIR}/src/tz.cpp")
    message(FATAL_ERROR
        "Howard Hinnant date source is missing. Clone v3.0.3 into external/date "
        "or set DATE_SOURCE_DIR to its checkout. See data/samples/README.md.")
endif()
find_package(Threads REQUIRED)
# Linux: use the local OS timezone database, with no curl or runtime downloads.
set(BUILD_TZ_LIB ON CACHE BOOL "Build date timezone library" FORCE)
set(USE_SYSTEM_TZ_DB ON CACHE BOOL "Use local OS timezone database" FORCE)
set(ENABLE_DATE_TESTING OFF CACHE BOOL "Disable upstream date tests" FORCE)
set(ENABLE_DATE_INSTALL OFF CACHE BOOL "Do not install bundled date" FORCE)
add_subdirectory("${DATE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/external/date" EXCLUDE_FROM_ALL)

function(enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(trading_core
    src/core/FixedPoint.cpp src/core/OrderManager.cpp
    src/portfolio/PortfolioAccounting.cpp
    src/execution/NextBarOpenFillModel.cpp src/execution/PaperBroker.cpp
    src/marketstate/MarketState.cpp
    src/strategy/DoNothingStrategy.cpp src/strategy/ScriptedStrategy.cpp
    src/strategy/MomentumStrategy.cpp src/strategy/StrategyFactory.cpp
    src/strategy/VNMomentumStrategy.cpp
    src/engine/SimulationClock.cpp src/engine/TradingEngine.cpp
    src/utils/Logger.cpp)
target_include_directories(trading_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(trading_core PUBLIC Threads::Threads)
enable_warnings(trading_core)
add_library(historical_data src/storage/CsvReader.cpp src/marketdata/HistoricalFeed.cpp)
target_link_libraries(historical_data PUBLIC trading_core date::date-tz)
enable_warnings(historical_data)
add_library(simulation_wiring src/simulation/HistoricalSimulationFactory.cpp src/simulation/SimulationFactory.cpp)
target_link_libraries(simulation_wiring PUBLIC historical_data trading_core)
enable_warnings(simulation_wiring)
include(CTest)
# if(BUILD_TESTING)
#     # foreach(suite FixedPoint Accounting CsvReader HistoricalFeed PaperBroker NextBarOpenFillModel SimulationContract
#     #         Clock Utils MarketState Strategy Factory ReplayIntegration VNMomentum)
#         foreach(suite  Accounting VNMomentum)
#         add_executable(${suite}Tests "tests/${suite}Tests.cpp")
#         target_link_libraries(${suite}Tests PRIVATE simulation_wiring)
#         target_compile_definitions(${suite}Tests PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/samples")
#         enable_warnings(${suite}Tests)
#         add_test(NAME ${suite}Tests COMMAND ${suite}Tests)
#     endforeach()
# endif()

# set(M1_BUILD_ID "unknown" CACHE STRING "Optional source revision/build identifier")
# add_library(cli_support apps/cli/CliConfiguration.cpp apps/cli/ResultExporter.cpp)
# target_include_directories(cli_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli")
# target_link_libraries(cli_support PUBLIC simulation_wiring)
# target_compile_definitions(cli_support PRIVATE M1_BUILD_ID="${M1_BUILD_ID}")
# enable_warnings(cli_support)
# add_executable(trading_cli apps/cli/main.cpp)
# target_link_libraries(trading_cli PRIVATE cli_support)
# enable_warnings(trading_cli)
# if(BUILD_TESTING)
#     find_package(Python3 COMPONENTS Interpreter REQUIRED)
#     add_test(NAME CliEndToEndTests COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/CliEndToEndTests.py"
#         "$<TARGET_FILE:trading_cli>" "${CMAKE_CURRENT_SOURCE_DIR}/data/samples")
# endif()

# option(TRADING_BUILD_GUI "Build the Qt historical trading desktop" ON)
if(TRADING_BUILD_GUI)
        # Where Qt is installed. Override with -DQT_ROOT=... or the QT_ROOT environment variable.
    set(QT_ROOT "$ENV{HOME}/Qt/6.5.3/gcc_64"
        CACHE PATH "Qt6 installation prefix (contains lib/cmake/Qt6)")
    if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
        set(QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt6 installation prefix" FORCE)
    endif()

    if(EXISTS "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
        message(STATUS "Using Qt6 from ${QT_ROOT}")
    else()
        message(WARNING "No Qt6 found in QT_ROOT='${QT_ROOT}'. "
                        "Falling back to system search paths.")
    endif()

    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

    set(CMAKE_AUTOMOC ON)
    add_executable(trading_gui
        apps/gui/main.cpp
        apps/gui/AppWindow.hpp
        apps/gui/TradingAPI.hpp 
        apps/gui/TradingAPI.cpp
        apps/gui/BackTestingPage.hpp
        apps/gui/BackTestingPage.cpp
        apps/gui/LiveTradingPage.hpp
        apps/gui/LiveTradingPage.cpp
        apps/gui/MarketChart.hpp 
        apps/gui/MarketChart.cpp)
    target_link_libraries(trading_gui PRIVATE Qt6::Widgets Qt6::Concurrent simulation_wiring)
    enable_warnings(trading_gui)
endif()