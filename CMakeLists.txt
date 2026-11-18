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
    src/core/FixedPoint.cpp 
    src/core/OrderManager.cpp
    src/portfolio/PortfolioAccounting.cpp
    src/execution/NextBarOpenFillModel.cpp 
    src/execution/PaperBroker.cpp
    src/marketstate/MarketState.cpp
    src/strategy/DoNothingStrategy.cpp 
    src/strategy/ScriptedStrategy.cpp
    src/strategy/MomentumStrategy.cpp 
    src/strategy/StrategyFactory.cpp
    src/strategy/VNMomentumStrategy.cpp
    src/engine/SimulationClock.cpp 
    src/engine/TradingEngine.cpp
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
# if(TRADING_BUILD_GUI)
#         # Where Qt is installed. Override with -DQT_ROOT=... or the QT_ROOT environment variable.
#     set(QT_ROOT "$ENV{HOME}/Qt/6.5.3/gcc_64"
#         CACHE PATH "Qt6 installation prefix (contains lib/cmake/Qt6)")
#     if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
#         set(QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt6 installation prefix" FORCE)
#     endif()

#     if(EXISTS "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
#         list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
#         message(STATUS "Using Qt6 from ${QT_ROOT}")
#     else()
#         message(WARNING "No Qt6 found in QT_ROOT='${QT_ROOT}'. "
#                         "Falling back to system search paths.")
#     endif()

#     find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)

#     set(CMAKE_AUTOMOC ON)
#     add_executable(trading_gui
#         apps/gui/main.cpp
#         apps/gui/AppWindow.hpp
#         apps/gui/TradingAPI.hpp 
#         apps/gui/TradingAPI.cpp
#         apps/gui/BackTestingPage.hpp
#         apps/gui/BackTestingPage.cpp
#         apps/gui/LiveTradingPage.hpp
#         apps/gui/LiveTradingPage.cpp
#         apps/gui/MarketChart.hpp 
#         apps/gui/MarketChart.cpp)
#     target_link_libraries(trading_gui PRIVATE Qt6::Widgets Qt6::Concurrent simulation_wiring)
#     enable_warnings(trading_gui)
# endif()

option(TRADING_BUILD_GUI "Build Qt desktop" ON)
option(TRADING_BUILD_LIVE "Build live/replay backend" ON)
if(TRADING_BUILD_GUI AND NOT TRADING_BUILD_LIVE)
    message(FATAL_ERROR "GUI requires live backend; set both flags OFF for historical libraries only")
endif()
if(TRADING_BUILD_GUI OR TRADING_BUILD_LIVE)
    set(QT_ROOT "$ENV{HOME}/Qt/6.5.3/gcc_64" CACHE PATH "Qt6 installation prefix")
    if(DEFINED ENV{QT_ROOT} AND NOT "$ENV{QT_ROOT}" STREQUAL "")
        set(QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt6 installation prefix" FORCE)
    endif()
    if(EXISTS "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${QT_ROOT}")
    endif()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network WebSockets)
endif()
if(TRADING_BUILD_LIVE)
    add_library(trading_live_core
        src/live/LiveSession.cpp src/live/LiveProcessor.cpp src/live/SyntheticSession.cpp
        src/marketdata/live/LiveBarBuilder.cpp src/marketdata/live/LiveConfiguration.cpp)
    target_link_libraries(trading_live_core PUBLIC trading_core)
    enable_warnings(trading_live_core)
    add_library(live_market_data
        apps/gui/live/ProviderNormalizer.cpp 
        apps/gui/live/ProviderStreamClient.cpp
        apps/gui/live/LivePipeline.cpp 
        apps/gui/live/SessionArchive.cpp
        src/recording/SessionRecorder.cpp)
    target_include_directories(live_market_data PUBLIC src apps/gui/live external/concurrentqueue)
    target_link_libraries(live_market_data PUBLIC trading_live_core Qt6::Core Qt6::Network Qt6::WebSockets)
    execute_process(COMMAND git describe --always --dirty WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE M3_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT M3_REVISION)
        set(M3_REVISION "source-archive")
    endif()
    file(GLOB_RECURSE M3_SOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/*.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/*.cpp")
    list(SORT M3_SOURCE_FILES)
    set(M3_SOURCE_DIGEST "")
    foreach(source_file IN LISTS M3_SOURCE_FILES)
        file(SHA256 "${source_file}" source_hash)
        file(RELATIVE_PATH source_name "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
        string(APPEND M3_SOURCE_DIGEST "${source_name}:${source_hash}\n")
    endforeach()
    string(SHA256 M3_SOURCE_HASH "${M3_SOURCE_DIGEST}")
    target_compile_definitions(live_market_data PRIVATE M3_SOURCE_HASH="${M3_SOURCE_HASH}" M3_BUILD_ID="${M3_REVISION}" M3_COMPILER_INFO="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_BUILD_TYPE}")
    enable_warnings(live_market_data)
    if(BUILD_TESTING)
        foreach(suite LiveData NetworkTransport M3Session)
            add_executable(${suite}Tests tests/live/${suite}Tests.cpp)
            target_link_libraries(${suite}Tests PRIVATE live_market_data)
            enable_warnings(${suite}Tests)
            add_test(NAME ${suite}Tests COMMAND ${suite}Tests)
            set_tests_properties(${suite}Tests PROPERTIES TIMEOUT 40)
        endforeach()
        add_executable(M3PerformanceBench tests/live/M3PerformanceBench.cpp)
        target_link_libraries(M3PerformanceBench PRIVATE live_market_data)
        enable_warnings(M3PerformanceBench)
        add_executable(M3HistoricalCompatibility tests/live/HistoricalCompatibility.cpp)
        target_link_libraries(M3HistoricalCompatibility PRIVATE simulation_wiring)
        target_compile_definitions(M3HistoricalCompatibility PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/samples")
        add_test(NAME M3HistoricalCompatibility COMMAND M3HistoricalCompatibility)
    endif()
endif()
if(TRADING_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Concurrent)
    set(CMAKE_AUTOMOC ON)
    add_library(desktop_backtest apps/gui/backtest/BackTestingPage.cpp apps/gui/backtest/BackTestingPage.hpp
        apps/gui/backtest/TradingAPI.cpp apps/gui/backtest/MarketChart.cpp)
    target_include_directories(desktop_backtest PUBLIC apps/gui apps/gui/backtest)
    target_link_libraries(desktop_backtest PUBLIC Qt6::Widgets Qt6::Concurrent simulation_wiring)
    enable_warnings(desktop_backtest)
    add_library(desktop_live apps/gui/live/LiveFeedController.cpp apps/gui/live/LiveTradingPage.cpp
        apps/gui/live/LiveMarketChart.cpp apps/gui/live/SessionReviewDialog.cpp
        apps/gui/live/LiveTradingPage.hpp apps/gui/live/SessionReviewDialog.hpp)
    target_include_directories(desktop_live PUBLIC apps/gui apps/gui/live)
    target_link_libraries(desktop_live PUBLIC Qt6::Widgets Qt6::Concurrent live_market_data)
    enable_warnings(desktop_live)
    add_executable(trading_gui apps/gui/main.cpp apps/gui/AppWindow.hpp)
    target_link_libraries(trading_gui PRIVATE desktop_backtest desktop_live)
    enable_warnings(trading_gui)
    if(BUILD_TESTING)
        add_executable(LiveGuiTests tests/live/LiveGuiTests.cpp)
        target_link_libraries(LiveGuiTests PRIVATE desktop_live desktop_backtest)
        add_test(NAME LiveGuiTests COMMAND LiveGuiTests)
        set_tests_properties(LiveGuiTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    endif()
endif()
