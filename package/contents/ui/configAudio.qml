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

    ScrollView {
        id: audioScrollView
        anchors.fill: parent
        contentWidth: availableWidth

    ColumnLayout {
        id: audioPage
        width: audioScrollView.availableWidth
        spacing: 0

        // ════════════════════════════════════════
        // WAKE WORD & AUDIO
        // ════════════════════════════════════════
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

            ComboBox {
                Kirigami.FormData.label: i18n("Whisper model:")
                model: [
                    { value: "tiny",  text: "Tiny (75MB — fastest, least accurate)" },
                    { value: "base",  text: "Base (142MB — good balance)" },
                    { value: "small", text: "Small (466MB — best accuracy)" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: {
                    var m = JarvisBackend.whisperModel
                    for (var i = 0; i < model.length; i++)
                        if (model[i].value === m) return i
                    return 0
                }
                onActivated: JarvisBackend.setWhisperModel(currentValue)
            }

            Label {
                text: i18n("Requires restart. Download models from huggingface.co/ggerganov/whisper.cpp and place in ~/.local/share/jarvis/")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Auto-start wake word detection:")
                checked: JarvisBackend.autoStartWakeWord
                onToggled: JarvisBackend.setAutoStartWakeWord(checked)
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Noise suppression (RNNoise):")
                checked: JarvisBackend.noiseSuppression
                onToggled: JarvisBackend.setNoiseSuppression(checked)
            }

            CheckBox {
                Kirigami.FormData.label: i18n("Continuous conversation mode:")
                checked: JarvisBackend.continuousMode
                onToggled: JarvisBackend.setContinuousMode(checked)
            }

            Label {
                visible: JarvisBackend.continuousMode
                text: i18n("After the wake word, the mic stays open for back-and-forth conversation. Say \"stop\", \"goodbye\", or \"thank you\" to end.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Label {
                text: i18n("Say the wake word to activate voice commands without clicking.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }

            Slider {
                id: voiceCmdSlider
                Kirigami.FormData.label: i18n("Max voice command length: %1 seconds", value.toFixed(0))
                from: 3; to: 30; stepSize: 1
                value: JarvisBackend.voiceCmdMaxSeconds
                onMoved: JarvisBackend.setVoiceCmdMaxSeconds(value)
            }

            Slider {
                id: silenceSlider
                Kirigami.FormData.label: i18n("Silence timeout: %1 ms", value.toFixed(0))
                from: 200; to: 3000; stepSize: 80
                value: JarvisBackend.silenceTimeoutMs
                onMoved: JarvisBackend.setSilenceTimeoutMs(value)
            }

            Label {
                text: i18n("How long to wait after you stop speaking before processing. Lower = faster response, higher = fewer false stops.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: Kirigami.Theme.disabledTextColor
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        // Bottom spacer
        Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
    }
    }
}
