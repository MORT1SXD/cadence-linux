cmake_minimum_required(VERSION 3.16)
project(cadence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET "gtk4>=4.12")
pkg_check_modules(ADW REQUIRED IMPORTED_TARGET "libadwaita-1>=1.5")
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET "libcurl>=7.85")
pkg_check_modules(JSON_GLIB REQUIRED IMPORTED_TARGET json-glib-1.0)

# Discord rich presence. The app works without it.
#   cmake .. -DENABLE_DISCORD=OFF
option(ENABLE_DISCORD "Discord rich presence (heart rate / VRChat status)" ON)

# The Discord Application ID is deliberately NOT stored in this repository.
# Source builds must bring their own:
#   cmake .. -DCADENCE_DISCORD_APP_ID=<your application id>
# Leaving it empty is valid: the app then shows "needs an Application ID" until
# one is written to ~/.config/cadence/state.ini.
set(CADENCE_DISCORD_APP_ID "" CACHE STRING
    "Discord Application ID baked into this build (empty = supply your own)")

set(CADENCE_SOURCES
    src/main.cpp
    src/battery_source.cpp
    src/system_source.cpp
    src/weather_source.cpp
    src/vrchat_status.cpp
    src/lyrics_source.cpp
    src/clock_source.cpp
    src/app_window.cpp
    src/osc_sender.cpp
    src/pulsoid_client.cpp
    src/media_reader.cpp
    src/settings.cpp
    src/formatter.cpp
    src/discord_presence.cpp
)

if(ENABLE_DISCORD)
    include(FetchContent)
    # pinned: the upstream repo is archived, so the tag must not float
    FetchContent_Declare(discord_rpc
        GIT_REPOSITORY https://github.com/discordapp/discord-rpc.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE
    )
    # discord-rpc's own options. these must be set before MakeAvailable.
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    # the repo is archived and its CMakeLists declares a cmake_minimum_required
    # older than the version in current cmake, which now hard-errors. this opts
    # back into the old policy behaviour for that subproject only.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    # discord-rpc also registers a `clangformat` target that part of the default
    # build. its checked-in .clang-format has a duplicate key that current
    # clang-format rejects, which fails the build for no useful reason. blanking
    # the variable stops find_program from finding it, so the target is never
    # created.
    set(CLANG_FORMAT_CMD "" CACHE FILEPATH "" FORCE)
    FetchContent_MakeAvailable(discord_rpc)
    list(APPEND CADENCE_SOURCES ${discord_rpc_SOURCE_DIR}/src)
    # its vendored rapidjson (1.1.0, from 2016) trips -Wtemplate-body on modern
    # gcc. it is third-party code we don't maintain, so silence the warning
    # rather than patching a vendored tree.
    target_compile_options(discord-rpc PRIVATE -Wno-template-body)
endif()

add_executable(cadence ${CADENCE_SOURCES})
install(TARGETS cadence RUNTIME DESTINATION bin)
install(FILES packaging/dev.cadence.app.desktop DESTINATION share/applications)
install(FILES packaging/dev.cadence.app.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES packaging/dev.cadence.app.metainfo.xml DESTINATION share/metainfo)
install(FILES LICENSE DESTINATION share/doc/cadence)
install(PROGRAMS install.sh DESTINATION libexec/cadence)

# Always defined, even when Discord is compiled out, so settings.hpp stays
# self-contained. Empty by default: nothing identifying is shipped in source.
target_compile_definitions(cadence PRIVATE
    CADENCE_DISCORD_APP_ID="${CADENCE_DISCORD_APP_ID}")

target_include_directories(cadence PRIVATE src)
target_link_libraries(cadence PRIVATE
    PkgConfig::GTK4 PkgConfig::ADW PkgConfig::LWS PkgConfig::CURL PkgConfig::JSON_GLIB)

if(ENABLE_DISCORD)
    target_compile_definitions(cadence PRIVATE CADENCE_DISCORD=1)
    # discord-rpc's headers aren't in a system include path
    target_include_directories(cadence PRIVATE ${discord_rpc_SOURCE_DIR}/include)
    target_link_libraries(cadence PRIVATE discord-rpc)
    if(UNIX AND NOT APPLE)
        target_link_libraries(cadence PRIVATE pthread dl)
    endif()
endif()
