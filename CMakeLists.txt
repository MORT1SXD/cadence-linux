cmake_minimum_required(VERSION 3.16)
project(cadence LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET "gtk4>=4.12")
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET "libadwaita-1>=1.5")
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET "libcurl>=7.85")
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)

find_program(GLIB_COMPILE_RESOURCES glib-compile-resources)
if(NOT GLIB_COMPILE_RESOURCES)
    message(FATAL_ERROR "glib-compile-resources is required")
endif()
set(CADENCE_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/cadence-resources.c")
add_custom_command(OUTPUT "${CADENCE_RESOURCES}"
    COMMAND "${GLIB_COMPILE_RESOURCES}" --generate-source
        --sourcedir "${CMAKE_CURRENT_SOURCE_DIR}/src" --target "${CADENCE_RESOURCES}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cadence.gresource.xml"
    DEPENDS src/cadence.gresource.xml src/cadence.png
    VERBATIM)
configure_file(src/cadence.png dev.cadence.app.png COPYONLY)

set(CADENCE_SOURCES
    src/main.cpp
    src/battery_source.cpp
    src/system_source.cpp
    src/weather_source.cpp
    src/vrchat_status.cpp
    src/lyrics_source.cpp
    src/clock_source.cpp
    src/app_window.cpp
    src/status_page.cpp
    src/tray.cpp
    src/window_close.cpp
    src/osc_sender.cpp
    src/pulsoid_client.cpp
    src/media_reader.cpp
    src/settings.cpp
    src/formatter.cpp
)

add_executable(cadence ${CADENCE_SOURCES} "${CADENCE_RESOURCES}")
install(TARGETS cadence RUNTIME DESTINATION bin)
install(FILES packaging/dev.cadence.app.desktop DESTINATION share/applications)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dev.cadence.app.png" DESTINATION share/icons/hicolor/48x48/apps)
install(FILES packaging/dev.cadence.app.metainfo.xml DESTINATION share/metainfo)
install(FILES LICENSE DESTINATION share/doc/cadence)
install(PROGRAMS install.sh DESTINATION libexec/cadence)

target_include_directories(cadence PRIVATE src)
target_link_libraries(cadence PRIVATE
    PkgConfig::GTK4 PkgConfig::ADW PkgConfig::LWS PkgConfig::CURL PkgConfig::JSON_GLIB
    Threads::Threads)
