cmake_minimum_required(VERSION 3.25)

project(jarvis-plasmoid VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Native architecture optimizations
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native -O3")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -O3")

set(QT_DEFAULT_MAJOR_VERSION 6)
set(QT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)

find_package(Qt6 REQUIRED COMPONENTS
    Quick
    Qml
    Network
    TextToSpeech
    Multimedia
    Concurrent
    DBus
)

find_package(KF6 REQUIRED COMPONENTS
    I18n
    Config
    Wallet
    Baloo
)

find_package(Plasma REQUIRED)

# whisper.cpp - lightweight local speech recognition
find_library(WHISPER_LIB NAMES whisper PATHS /usr/local/lib /usr/lib)
find_path(WHISPER_INC whisper.h PATHS /usr/local/include /usr/include)
if(NOT WHISPER_LIB OR NOT WHISPER_INC)
    message(FATAL_ERROR "whisper.cpp not found. Install it: https://github.com/ggerganov/whisper.cpp")
endif()
set(WHISPER_LIBRARIES ${WHISPER_LIB})
set(WHISPER_INCLUDE_DIRS ${WHISPER_INC})
message(STATUS "Found whisper: ${WHISPER_LIB}")

# PipeWire for mic-busy detection
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
message(STATUS "Found PipeWire: ${PIPEWIRE_LIBRARIES}")

# RNNoise for noise suppression
pkg_check_modules(RNNOISE REQUIRED rnnoise)
message(STATUS "Found RNNoise: ${RNNOISE_LIBRARIES}")

# Install the plasmoid package
plasma_install_package(package org.kde.plasma.jarvis)

# Install icons into hicolor theme
ecm_install_icons(
    ICONS
        icons/16-apps-jarvis-ai.png
        icons/22-apps-jarvis-ai.png
        icons/32-apps-jarvis-ai.png
        icons/48-apps-jarvis-ai.png
        icons/64-apps-jarvis-ai.png
        icons/128-apps-jarvis-ai.png
        icons/256-apps-jarvis-ai.png
        icons/sc-apps-jarvis-ai.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

# libpiper for native TTS synthesis
# Try system install first, build from source if not found
find_library(PIPER_LIB NAMES piper PATHS /usr/local/lib64 /usr/local/lib)
find_path(PIPER_INC piper.h PATHS /usr/local/include)
if(PIPER_LIB AND PIPER_INC)
    set(PIPER_FOUND TRUE)
    message(STATUS "Found libpiper: ${PIPER_LIB}")
else()
    message(STATUS "libpiper not found — building from source (this may take a few minutes)")
    include(FetchContent)
    FetchContent_Declare(
        piper1_gpl
        GIT_REPOSITORY https://github.com/OHF-Voice/piper1-gpl.git
        GIT_TAG main
    )
    FetchContent_Populate(piper1_gpl)
    # EXCLUDE_FROM_ALL prevents upstream's install() rules from running
    add_subdirectory(${piper1_gpl_SOURCE_DIR}/libpiper ${piper1_gpl_BINARY_DIR} EXCLUDE_FROM_ALL)

    # onnxruntime headers require exceptions, and symbols must be exported
    target_compile_options(piper PRIVATE -fexceptions -fvisibility=default)

    # Point to the built library and header
    set(PIPER_LIB piper)
    set(PIPER_INC ${piper1_gpl_SOURCE_DIR}/libpiper/include)
    set(PIPER_FOUND TRUE)
    set(PIPER_BUILT_FROM_SOURCE TRUE)

    # Override piper's install to use proper directories
    set_target_properties(piper PROPERTIES
        INSTALL_RPATH_USE_LINK_PATH TRUE)
    install(TARGETS piper DESTINATION ${KDE_INSTALL_LIBDIR})
    install(FILES ${piper1_gpl_SOURCE_DIR}/libpiper/include/piper.h
            DESTINATION ${KDE_INSTALL_INCLUDEDIR})
    install(DIRECTORY ${CMAKE_BINARY_DIR}/espeak_ng-install/share/espeak-ng-data
            DESTINATION ${KDE_INSTALL_DATADIR})
    message(STATUS "libpiper will be built from source")
endif()

add_subdirectory(src/plugin)
