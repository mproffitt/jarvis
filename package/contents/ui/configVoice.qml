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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Fixed header: voice info + filters ──
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

        RowLayout {
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Label { text: i18n("Language:") }
            ComboBox {
                id: langFilterCombo
                model: [
                    { text: i18n("All Languages"), value: "" },
                    { text: i18n("English"), value: "en" },
                    { text: i18n("German"), value: "de" },
                    { text: i18n("French"), value: "fr" },
                    { text: i18n("Spanish"), value: "es" },
                    { text: i18n("Italian"), value: "it" },
                    { text: i18n("Portuguese"), value: "pt" },
                    { text: i18n("Polish"), value: "pl" },
                    { text: i18n("Dutch"), value: "nl" },
                    { text: i18n("Russian"), value: "ru" },
                    { text: i18n("Chinese"), value: "zh" },
                    { text: i18n("Czech"), value: "cs" },
                    { text: i18n("Finnish"), value: "fi" },
                    { text: i18n("Hungarian"), value: "hu" },
                    { text: i18n("Norwegian"), value: "no" },
                    { text: i18n("Swedish"), value: "sv" },
                    { text: i18n("Turkish"), value: "tr" },
                    { text: i18n("Ukrainian"), value: "uk" },
                    { text: i18n("Vietnamese"), value: "vi" },
                    { text: i18n("Catalan"), value: "ca" },
                    { text: i18n("Greek"), value: "el" },
                    { text: i18n("Arabic"), value: "ar" },
                    { text: i18n("Korean"), value: "ko" },
                    { text: i18n("Japanese"), value: "ja" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 1
                onActivated: JarvisBackend.searchVoices(currentValue, qualityFilterCombo.currentValue)
            }

            Label { text: i18n("Source:") }
            ComboBox {
                id: sourceFilterCombo
                model: [
                    { text: i18n("All"), value: "" },
                    { text: i18n("Official"), value: "official" },
                    { text: i18n("Community"), value: "community" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
            }

            Label { text: i18n("Quality:") }
            ComboBox {
                id: qualityFilterCombo
                model: [
                    { text: i18n("All Qualities"), value: "" },
                    { text: i18n("High"), value: "high" },
                    { text: i18n("Medium"), value: "medium" },
                    { text: i18n("Low"), value: "low" },
                    { text: i18n("Extra Low"), value: "x_low" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
                onActivated: JarvisBackend.searchVoices(langFilterCombo.currentValue, currentValue)
            }
        }

        // ── Scrollable voice cards ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: voiceListView
                clip: true
                spacing: Kirigami.Units.smallSpacing
                model: {
                    var all = JarvisBackend.availableTtsVoices
                    var src = sourceFilterCombo.currentValue
                    if (!src || src.length === 0) return all
                    var filtered = []
                    for (var i = 0; i < all.length; i++)
                        if (all[i].source === src) filtered.push(all[i])
                    return filtered
                }

                delegate: Kirigami.AbstractCard {
                    width: voiceListView.width - Kirigami.Units.smallSpacing * 2
                    x: Kirigami.Units.smallSpacing

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
        }

        // ── Fixed bottom: synthesis settings ──
        Kirigami.Heading {
            text: i18n("Voice Synthesis Settings")
            level: 4
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.largeSpacing
        }

        Label {
            text: i18n("Speech rate: %1").arg(rateSlider.value.toFixed(2))
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }
        Slider {
            id: rateSlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: -1.0; to: 1.0; stepSize: 0.05
            value: 0.05
            onMoved: JarvisBackend.setTtsRate(value)
        }

        Label {
            text: i18n("Speech pitch: %1").arg(pitchSlider.value.toFixed(2))
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }
        Slider {
            id: pitchSlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: -1.0; to: 1.0; stepSize: 0.05
            value: -0.1
            onMoved: JarvisBackend.setTtsPitch(value)
        }

        Label {
            text: i18n("Volume: %1%").arg((volumeSlider.value * 100).toFixed(0))
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }
        Slider {
            id: volumeSlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: 0.0; to: 1.0; stepSize: 0.05
            value: 0.85
            onMoved: JarvisBackend.setTtsVolume(value)
        }

        Button {
            text: i18n("Test Current Voice")
            icon.name: "media-playback-start"
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            onClicked: JarvisBackend.testVoice(JarvisBackend.currentVoiceName)
        }
    }
}
