# Changelog

All notable changes to J.A.R.V.I.S. Plasmoid will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.1] - 2026-03-06

### Changed
- **LLM Streaming**: Switched from blocking request to SSE token streaming — response text appears in real-time as the LLM generates it
- **Sentence-based TTS Pipeline**: LLM response is split into sentences on-the-fly; first complete sentence is spoken immediately while the LLM continues generating
- **Piper TTS Refactor**: Sentence-queue architecture — each sentence is synthesized and played sequentially, no more single monolithic shell command per utterance
- **Sentence Splitting**: New `splitIntoSentences()` utility splits on `.!?;:` punctuation boundaries for natural TTS chunking
- **Stop Behavior**: `stop()` now clears the entire sentence queue instantly, improving responsiveness

### Added
- `streamingResponse` Q_PROPERTY — QML can bind to real-time token output during LLM generation
- `speakSentence()` method in TTS — allows external callers (streaming pipeline) to enqueue individual sentences
- `trySpeakCompleteSentences()` — monitors streamed text and dispatches complete sentences to TTS as they form
- `finalizeStreamingResponse()` — handles end-of-stream: speaks remaining text, executes ACTION blocks

### Optimized
- Compilation flags: `-march=native -O3` for native architecture optimization
- Whisper: already uses `WHISPER_SAMPLING_GREEDY` strategy, `audio_ctx=512`, 2 threads, amplitude threshold
- Removed unused `QMediaPlayer`/`QAudioOutput` from TTS (cleanup from 0.1.0)
- Cached Piper binary path — no filesystem scan per utterance

### Fixed
- TTS mute now properly stops all queued sentences, not just current playback

## [0.1.0] - 2026-03-05

### Added
- Initial public release
- Local LLM integration via llama.cpp (OpenAI-compatible API)
- Wake word detection ("Jarvis") using whisper.cpp (tiny model, CPU-only)
- Voice command transcription with automatic language detection
- Piper TTS with downloadable voice models (espeak-ng fallback)
- Real-time system monitoring: CPU, RAM, temperature, uptime
- 14 built-in voice commands (open apps, lock screen, volume, screenshot)
- Custom voice command mappings with CRUD management
- Advanced system interaction via LLM — structured ACTION blocks:
  - `run_command` — execute shell commands
  - `open_terminal` — open terminal with a command
  - `write_file` — create/write files with content
  - `open_app` — launch GUI applications
  - `open_url` — open URLs in browser
  - `type_text` — type text into focused window via xdotool
- Reminder and timer system with quick presets
- Downloadable LLM models (GGUF) and TTS voices from HuggingFace
- Iron Man HUD-style UI with arc reactor animation, waveform visualizer
- KDE Plasma 6 configuration dialog with General, Voice Commands, and J.A.R.V.I.S. tabs
- 100% local and private — no cloud, no subscriptions, no data leaves your machine

### Technical
- C++23 backend with modular architecture (settings, TTS, audio, system, commands)
- QML frontend with Kirigami components
- CMake build system with ECM integration
- Qt 6 (Quick, Qml, Network, TextToSpeech, Multimedia, Concurrent)
- KF6::I18n for internationalization support

[0.1.1]: https://github.com/novik133/jarvis/releases/tag/v0.1.1
[0.1.0]: https://github.com/novik133/jarvis/releases/tag/v0.1.0
