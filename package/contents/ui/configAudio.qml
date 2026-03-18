import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.jarvis 1.0

Item {
    id: configAudioRoot
    property string title: i18n("Audio")
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Fixed top: settings ──
        Kirigami.FormLayout {
            Component.onCompleted: { for (var i = 0; i < children.length; i++) { if (children[i].hasOwnProperty("columns")) { children[i].anchors.horizontalCenter = undefined; children[i].anchors.left = left; children[i].anchors.leftMargin = Qt.binding(function() { return Kirigami.Units.smallSpacing; }); } } }
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Wake Word & Audio")
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Wake word:")
                spacing: Kirigami.Units.smallSpacing
                TextField {
                    id: wakeWordField
                    text: JarvisBackend.wakeWord
                    placeholderText: i18n("jarvis")
                    Layout.fillWidth: true
                    onAccepted: JarvisBackend.setWakeWord(text)
                }
                Button {
                    text: i18n("Apply")
                    icon.name: "dialog-ok-apply"
                    onClicked: JarvisBackend.setWakeWord(wakeWordField.text)
                }
            }

            Label {
                Kirigami.FormData.label: i18n("Whisper model:")
                text: JarvisBackend.whisperModel
                font.bold: true
            }

            GridLayout {
                columns: 2
                columnSpacing: Kirigami.Units.largeSpacing * 2
                rowSpacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("GPU acceleration")
                    checked: JarvisBackend.whisperGpu
                    onToggled: JarvisBackend.setWhisperGpu(checked)
                }
                CheckBox {
                    text: i18n("Auto-start wake word")
                    checked: JarvisBackend.autoStartWakeWord
                    onToggled: JarvisBackend.setAutoStartWakeWord(checked)
                }
                CheckBox {
                    text: i18n("Noise suppression (RNNoise)")
                    checked: JarvisBackend.noiseSuppression
                    onToggled: JarvisBackend.setNoiseSuppression(checked)
                }
                CheckBox {
                    text: i18n("Continuous conversation")
                    checked: JarvisBackend.continuousMode
                    onToggled: JarvisBackend.setContinuousMode(checked)
                }
            }

            Label {
                visible: JarvisBackend.continuousMode
                text: i18n("Continuous mode: mic stays open after wake word. Say \"stop\" or \"goodbye\" to end.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        // ── Whisper model header ──
        Kirigami.Separator { Layout.fillWidth: true; Layout.topMargin: Kirigami.Units.largeSpacing }
        Label { text: i18n("Available Whisper Models"); font.bold: true; Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.topMargin: Kirigami.Units.smallSpacing }
        Component.onCompleted: JarvisBackend.fetchWhisperModels()

        // ── Scrollable whisper model cards ──
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: whisperListView
                clip: true
                spacing: Kirigami.Units.smallSpacing
                model: JarvisBackend.whisperModelList

                delegate: Kirigami.AbstractCard {
                    width: whisperListView.width - Kirigami.Units.smallSpacing * 2
                    x: Kirigami.Units.smallSpacing

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Label { text: modelData.name; font.bold: true }
                        Label { text: modelData.size; color: Kirigami.Theme.disabledTextColor; font.pointSize: Kirigami.Theme.smallFont.pointSize }
                        Item { Layout.fillWidth: true }
                        Label {
                            visible: modelData.installed && modelData.file === ("ggml-" + JarvisBackend.whisperModel + ".bin")
                            text: i18n("In use")
                            color: Kirigami.Theme.positiveTextColor
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                        Button {
                            visible: !modelData.installed || modelData.file !== ("ggml-" + JarvisBackend.whisperModel + ".bin")
                            text: modelData.installed ? i18n("Use") : i18n("Download")
                            icon.name: modelData.installed ? "dialog-ok-apply" : "download"
                            enabled: !JarvisBackend.downloading
                            onClicked: {
                                if (modelData.installed) {
                                    var name = modelData.file.replace("ggml-", "").replace(".bin", "")
                                    JarvisBackend.setWhisperModel(name)
                                } else {
                                    JarvisBackend.downloadWhisperModel(modelData.file)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Fixed bottom: download progress + sliders ──
        ProgressBar { visible: JarvisBackend.downloading; value: JarvisBackend.downloadProgress; Layout.fillWidth: true; Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.largeSpacing }
        Label { visible: JarvisBackend.downloading; text: JarvisBackend.downloadStatus; Layout.leftMargin: Kirigami.Units.largeSpacing; color: Kirigami.Theme.disabledTextColor; font.pointSize: Kirigami.Theme.smallFont.pointSize }
        Kirigami.Separator { Layout.fillWidth: true; Layout.topMargin: Kirigami.Units.largeSpacing }

        Label {
            text: i18n("Max voice command length: %1 seconds").arg(voiceCmdSlider.value.toFixed(0))
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.topMargin: Kirigami.Units.smallSpacing
        }
        Slider {
            id: voiceCmdSlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: 3; to: 30; stepSize: 1
            value: JarvisBackend.voiceCmdMaxSeconds
            onMoved: JarvisBackend.setVoiceCmdMaxSeconds(value)
        }

        Label {
            text: i18n("Silence timeout: %1 ms").arg(silenceSlider.value.toFixed(0))
            Layout.leftMargin: Kirigami.Units.largeSpacing
        }
        Slider {
            id: silenceSlider
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            from: 200; to: 3000; stepSize: 80
            value: JarvisBackend.silenceTimeoutMs
            onMoved: JarvisBackend.setSilenceTimeoutMs(value)
        }

        Label {
            text: i18n("How long to wait after you stop speaking before processing. Lower = faster, higher = fewer false stops.")
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            Layout.bottomMargin: Kirigami.Units.largeSpacing
            color: Kirigami.Theme.disabledTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
        }
    }
}
