import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.jarvis 1.0

Item {
    id: configVoiceRoot
    property string title: i18n("Voice")
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    clip: true

    ScrollView {
        id: voiceScrollView
        anchors.fill: parent
        contentWidth: availableWidth

    ColumnLayout {
        id: voicePage
        width: voiceScrollView.availableWidth
        spacing: 0

        // ════════════════════════════════════════
        // TTS VOICES
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("TTS Voices (Piper)")
            }

            Label {
                text: i18n("Choose a voice for speech synthesis. Download a voice, then press Play to preview it.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                Kirigami.FormData.label: i18n("Active voice:")
                text: JarvisBackend.currentVoiceName || i18n("None")
                font.bold: true
            }
        }

        Repeater {
            model: JarvisBackend.availableTtsVoices
            delegate: Kirigami.AbstractCard {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Label {
                                text: modelData.name
                                font.bold: true
                            }
                            Label {
                                text: modelData.lang
                                color: Kirigami.Theme.disabledTextColor
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                            }
                            Kirigami.Icon {
                                visible: modelData.active
                                source: "emblem-default"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                            }
                        }
                        Label {
                            text: modelData.desc
                            color: Kirigami.Theme.disabledTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                    }
                    Button {
                        visible: modelData.downloaded
                        text: i18n("Play")
                        icon.name: "media-playback-start"
                        flat: true
                        ToolTip.text: i18n("Preview this voice")
                        ToolTip.visible: hovered
                        onClicked: JarvisBackend.testVoice(modelData.id)
                    }
                    Button {
                        text: modelData.active ? i18n("Active") : (modelData.downloaded ? i18n("Activate") : i18n("Download"))
                        icon.name: modelData.active ? "checkmark" : (modelData.downloaded ? "media-playback-start" : "download")
                        enabled: !modelData.active && !JarvisBackend.downloading
                        highlighted: modelData.active
                        onClicked: {
                            if (modelData.downloaded) JarvisBackend.setActiveTtsVoice(modelData.id)
                            else JarvisBackend.downloadTtsVoice(modelData.id)
                        }
                    }
                }
            }
        }

        Button {
            text: i18n("Fetch More Voices")
            icon.name: "list-add"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            onClicked: JarvisBackend.fetchMoreVoices()
        }

        // ════════════════════════════════════════
        // VOICE SYNTHESIS SETTINGS
        // ════════════════════════════════════════
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Voice Synthesis Settings")
            }

            Slider {
                id: rateSlider
                Kirigami.FormData.label: i18n("Speech rate: %1", value.toFixed(2))
                from: -1.0; to: 1.0; stepSize: 0.05
                value: 0.05
                onMoved: JarvisBackend.setTtsRate(value)
            }

            Slider {
                id: pitchSlider
                Kirigami.FormData.label: i18n("Speech pitch: %1", value.toFixed(2))
                from: -1.0; to: 1.0; stepSize: 0.05
                value: -0.1
                onMoved: JarvisBackend.setTtsPitch(value)
            }

            Slider {
                id: volumeSlider
                Kirigami.FormData.label: i18n("Volume: %1%", (value * 100).toFixed(0))
                from: 0.0; to: 1.0; stepSize: 0.05
                value: 0.85
                onMoved: JarvisBackend.setTtsVolume(value)
            }

            Button {
                text: i18n("Test Current Voice")
                icon.name: "media-playback-start"
                onClicked: JarvisBackend.testVoice(JarvisBackend.currentVoiceName)
            }
        }

        // Bottom spacer
        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }
    }
}
