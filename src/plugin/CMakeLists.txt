set(jarvis_plugin_SRCS
    jarvisplugin.cpp
    jarvisbackend.cpp
    settings/jarvissettings.cpp
    settings/jarvisoauth.cpp
    tts/jarvisTts.cpp
    audio/jarvisaudio.cpp
    audio/micmonitor.cpp
    system/jarvissystem.cpp
    commands/jarviscommands.cpp
    rag/jarvisrag.cpp
    mcp/jarvismcp.cpp
)

add_library(jarvis_plugin SHARED ${jarvis_plugin_SRCS})

target_include_directories(jarvis_plugin PRIVATE
    ${WHISPER_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${RNNOISE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(jarvis_plugin
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::TextToSpeech
    Qt6::Multimedia
    Qt6::Concurrent
    Qt6::DBus
    Plasma::Plasma
    KF6::I18n
    KF6::Wallet
    KF6::Baloo
    ${WHISPER_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${RNNOISE_LIBRARIES}
)

if(PIPER_FOUND)
    target_include_directories(jarvis_plugin PRIVATE ${PIPER_INC})
    target_link_libraries(jarvis_plugin ${PIPER_LIB})
    target_compile_definitions(jarvis_plugin PRIVATE HAVE_LIBPIPER)
    if(PIPER_BUILT_FROM_SOURCE)
        add_dependencies(jarvis_plugin piper)
    endif()
endif()

target_compile_features(jarvis_plugin PUBLIC cxx_std_23)

install(TARGETS jarvis_plugin DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/plasma/jarvis)
install(FILES qmldir DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/plasma/jarvis)
