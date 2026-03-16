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
    Concurrent
    DBus
)

find_package(KF6 REQUIRED COMPONENTS
    I18n
    Config
    Wallet
    Baloo
    FileMetaData
)

find_package(Plasma REQUIRED)

# whisper.cpp - lightweight local speech recognition
# Try system install first; if not found or if CUDA is available (and system
# whisper lacks it), build from source with GPU support auto-detected.
option(WHISPER_BUILD_FROM_SOURCE "Force building whisper.cpp from source" OFF)

find_library(WHISPER_LIB NAMES whisper PATHS /usr/local/lib64 /usr/local/lib /usr/lib64 /usr/lib)
find_path(WHISPER_INC whisper.h PATHS /usr/local/include /usr/include)

# Check if CUDA toolkit is available
find_program(NVCC_EXECUTABLE nvcc PATHS /usr/local/cuda/bin /usr/local/cuda-13.2/bin ENV PATH)
if(NVCC_EXECUTABLE)
    set(CUDA_AVAILABLE TRUE)
    message(STATUS "CUDA compiler found: ${NVCC_EXECUTABLE}")
else()
    set(CUDA_AVAILABLE FALSE)
endif()

# Check if system whisper has CUDA support
set(SYSTEM_WHISPER_HAS_CUDA FALSE)
if(WHISPER_LIB)
    execute_process(
        COMMAND ldd ${WHISPER_LIB}
        OUTPUT_VARIABLE _whisper_ldd
        ERROR_QUIET)
    if(_whisper_ldd MATCHES "libcuda|libcublas|libggml-cuda")
        set(SYSTEM_WHISPER_HAS_CUDA TRUE)
    endif()
endif()

# Build from source if: forced, not found, or CUDA available but system whisper lacks it
if(WHISPER_BUILD_FROM_SOURCE
   OR NOT WHISPER_LIB OR NOT WHISPER_INC
   OR (CUDA_AVAILABLE AND NOT SYSTEM_WHISPER_HAS_CUDA))

    if(CUDA_AVAILABLE AND NOT SYSTEM_WHISPER_HAS_CUDA)
        message(STATUS "System whisper.cpp lacks CUDA — building from source with GPU support")
    elseif(NOT WHISPER_LIB)
        message(STATUS "whisper.cpp not found — building from source")
    else()
        message(STATUS "Building whisper.cpp from source (forced)")
    endif()

    include(FetchContent)
    FetchContent_Declare(
        whisper_cpp
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
        GIT_TAG v1.7.6
    )

    # Configure whisper.cpp build options
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    if(CUDA_AVAILABLE)
        set(GGML_CUDA ON CACHE BOOL "" FORCE)
        set(CMAKE_CUDA_COMPILER ${NVCC_EXECUTABLE} CACHE FILEPATH "" FORCE)
        message(STATUS "Enabling CUDA for whisper.cpp (${NVCC_EXECUTABLE})")
    endif()

    # Populate first, then add subdirectory with relaxed flags
    FetchContent_Populate(whisper_cpp)

    # Disable upstream install rules — we handle installation ourselves
    set(GGML_INSTALL OFF CACHE BOOL "" FORCE)
    set(WHISPER_INSTALL OFF CACHE BOOL "" FORCE)

    # Temporarily relax compiler flags for whisper.cpp — KDE's strict
    # settings conflict with ggml's exception handling code
    set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
    set(_saved_c_flags "${CMAKE_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "-fpermissive -Wno-error")
    set(CMAKE_C_FLAGS "-Wno-error")

    add_subdirectory(${whisper_cpp_SOURCE_DIR} ${whisper_cpp_BINARY_DIR} EXCLUDE_FROM_ALL)

    set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}")
    set(CMAKE_C_FLAGS "${_saved_c_flags}")

    set(WHISPER_LIBRARIES whisper)
    set(WHISPER_INCLUDE_DIRS
        ${whisper_cpp_SOURCE_DIR}/include
        ${whisper_cpp_SOURCE_DIR}/ggml/include)
    set(WHISPER_BUILT_FROM_SOURCE TRUE)

    # Install whisper and ggml libraries — override upstream install rules
    # to use KDE_INSTALL_LIBDIR and avoid missing header errors
    foreach(_tgt whisper ggml ggml-base ggml-cpu)
        if(TARGET ${_tgt})
            set_target_properties(${_tgt} PROPERTIES
                INSTALL_RPATH_USE_LINK_PATH TRUE)
            install(TARGETS ${_tgt} LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
        endif()
    endforeach()
    if(TARGET ggml-cuda)
        set_target_properties(ggml-cuda PROPERTIES
            INSTALL_RPATH_USE_LINK_PATH TRUE)
        install(TARGETS ggml-cuda LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
    endif()

    message(STATUS "whisper.cpp will be built from source (CUDA: ${CUDA_AVAILABLE})")
else()
    set(WHISPER_LIBRARIES ${WHISPER_LIB})
    set(WHISPER_INCLUDE_DIRS ${WHISPER_INC})
    message(STATUS "Found system whisper: ${WHISPER_LIB} (CUDA: ${SYSTEM_WHISPER_HAS_CUDA})")
endif()

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
