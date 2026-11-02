cmake_minimum_required(VERSION 3.28)
project(nexus-twitch-core VERSION 1.3.1 LANGUAGES CXX RC)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Network)
find_package(CURL REQUIRED CONFIG)
find_package(libobs REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(S vendor/src)
add_library(nexus-core-engine STATIC
 ${S}/twitch/protocol.cpp ${S}/twitch/transport.cpp ${S}/twitch/session-store.cpp ${S}/twitch/ticker-feed.cpp
 ${S}/twitch/service.cpp ${S}/twitch/browser-authorization.cpp
 ${S}/core/plugin-core.cpp ${S}/events/event-engine.cpp ${S}/events/simulation.cpp
 ${S}/execution/execution-queue.cpp ${S}/execution/presentation-queue.cpp
 ${S}/state/shared-state.cpp ${S}/state/text-cache.cpp ${S}/state/daily-totals.cpp
 ${S}/adapters/json-config-store.cpp ${S}/adapters/json-daily-store.cpp ${S}/adapters/obs-logger.cpp
 ${S}/visual/asset-manager.cpp ${S}/visual/text-layout.cpp ${S}/visual/visual-engine.cpp
 ${S}/localization.cpp vendor/common/localization/language.cpp)
target_include_directories(nexus-core-engine PUBLIC ${S} vendor/common shared ../shared)
target_link_libraries(nexus-core-engine PUBLIC Qt6::Core Qt6::Gui Qt6::Network CURL::libcurl OBS::libobs crypt32)
add_library(nexus-twitch-core SHARED ${S}/twitch/shared-provider.cpp ${S}/twitch/shared-runtime.cpp shared/core.rc)
target_include_directories(nexus-twitch-core PRIVATE shared)
target_compile_definitions(nexus-twitch-core PRIVATE NEXUS_CORE_BUILD)
target_link_libraries(nexus-twitch-core PUBLIC nexus-core-engine)
qt_add_resources(nexus-core-engine oto_languages PREFIX /nexus BASE vendor FILES
 vendor/data/locale/en-US.ini vendor/data/locale/es-ES.ini vendor/data/locale/it-IT.ini
 vendor/data/locale/fr-FR.ini vendor/data/locale/de-DE.ini)
enable_testing()
add_executable(legacy-core-tests tests/legacy-core-tests.cpp tests/twitch-core-client.cpp)
target_include_directories(legacy-core-tests PRIVATE tests)
target_link_libraries(legacy-core-tests PRIVATE nexus-twitch-core)
add_test(NAME legacy-abi6-lifecycle COMMAND legacy-core-tests)
add_executable(ticker-feed-tests tests/ticker-feed-tests.cpp)
target_link_libraries(ticker-feed-tests PRIVATE nexus-core-engine)
add_test(NAME ticker-feed-state COMMAND ticker-feed-tests)
foreach(test legacy-abi6-lifecycle ticker-feed-state)
 set_tests_properties(${test} PROPERTIES TIMEOUT 30 ENVIRONMENT_MODIFICATION
  "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:CURL::libcurl_shared>;PATH=path_list_prepend:$<TARGET_FILE_DIR:OBS::libobs>")
endforeach()
