# Audiotools – architecture

One firmware for the M5Stack Cardputer ADV containing several audio apps.
A shared core does the hard work; each app is a thin layer on top of it.

## Layers

```
┌──────────────────────── APPS (one file each) ─────────────────────────┐
│ Decibel │ Tuner │ PolyTune │ Spectrum │ BPM │ Vocal │ Recorder │ Monitor │
└────────────────────────────────────────────────────────────────────────┘
┌──────────── DSP library (lib/dsp: pure C++, no hardware) ─────────────┐
│ level (RMS, dB SPL, A-weighting, Leq) · pitch (YIN) · fft · bands      │
│ onset/beat (BPM) · notes (Hz -> note, cents) · polytune                │
└────────────────────────────────────────────────────────────────────────┘
┌──────────── Services (src/services) ──────────────────────────────────┐
│ audio_in (gapless sample stream) · settings (NVS: calibration, A4...)  │
│ storage (SD card, WAV, CSV) · ui (canvas, header, widgets)             │
└────────────────────────────────────────────────────────────────────────┘
┌──────────── Hardware (src/hw + M5Cardputer library) ──────────────────┐
│ es8311 (codec gain) · display · keyboard · microphone · speaker        │
└────────────────────────────────────────────────────────────────────────┘
```

Rules:
- **DSP code never includes Arduino or M5 headers.** It only gets arrays of
  samples, so it can be unit-tested on a PC (`pio test -e native`) with
  synthetic signals (e.g. a 440 Hz sine must be measured as 440.0 Hz).
- **Apps never talk to the hardware directly**: they get audio from
  `audio_in`, draw through `ui` and store through `storage`/`settings`.
- Only **one app runs at a time**. Big buffers (FFT, recordings) are
  allocated in `enter()` and freed in `exit()` – the ESP32-S3 in the
  Cardputer has no PSRAM (about 320 KB RAM).

## App interface

Every app implements `App` (`src/app.h`):

| Method | Called |
|--------|--------|
| `name()` | menu title |
| `sampleRate()`, `micGain()` | before `enter()`, to set up the microphone |
| `enter()` / `exit()` | when the app is opened / closed |
| `process(samples, count)` | for every chunk of new audio (gapless stream) |
| `draw(canvas)` | about 30× per second |
| `onKey(key)` | on a key press (`Esc` returns to the launcher, handled by main) |

Adding an app = one new file in `src/apps/`, its declaration in
`src/apps/apps.h` and one line (title, description, icon) in `src/launcher.cpp`.

## Launcher
A carousel like the Bruce firmware main menu: one app per screen with a big
icon drawn from simple shapes, `,` / `/` browse, `Enter` opens, `Esc` returns.
The last opened app is remembered in NVS. Apps that are not written yet are
shown as "coming soon". The arrow keys are not used to switch between
running apps: apps need the arrows themselves, and a long recording must not
be stopped by an accidental key press.

## Audio input

`audio_in` keeps the microphone recording all the time while an app is open:
two buffers are queued with `M5Cardputer.Mic.record()`; whenever one is
filled, the Mic release callback copies it into a FreeRTOS stream buffer and
queues it again. The main loop reads the stream buffer and passes the samples
to the app. No sound is lost between blocks (needed for BPM and recording).

- Arduino core **3.2.0** is required (see README).
- The microphone and the speaker share the I2S bus: an app that plays a sound
  (alarm, reference tone) must stop `audio_in`, play, and start it again.
- `Mic.begin()` resets the codec gain, so `audio_in` re-applies it.

## Shared parts

| Part | Used by |
|------|---------|
| pitch detection (YIN) + needle gauge | tuner, vocal trainer |
| FFT | spectrum, PolyTune, BPM |
| level (dB SPL, Leq, A-weighting) | decibel meter, monitor |
| SD card + WAV writer | recorder, monitor |

## Roadmap

| # | Step | Why in this order |
|---|------|-------------------|
| 0 | Skeleton: menu, gapless audio input, app interface, settings, PC tests | everything else builds on it |
| 1 | Decibel meter: dB SPL, calibration, max, Leq, A-weighting | simplest app, validates the skeleton |
| 2 | Tuner: needle gauge ±50 cents in 5-cent steps, green centre ±3 cents, big note name | YIN already works; also verify the real sample rate (440 Hz reads 441.0 Hz) |
| 3 | Vocal intonation trainer | reuses the tuner |
| 4 | Spectrum analyser: green/yellow/red bars with peak hold | introduces FFT |
| 5 | PolyTune (all strings at once) | FFT + tuner knowledge; needs fine frequency resolution (E2–A2 are 28 Hz apart) |
| 6 | BPM detector (listening and tap tempo) | FFT/onsets, gapless audio |
| 7 | Recorder to SD card | SD + WAV |
| 8 | Monitor: night/snoring log, long-term level graph, sound alarm | level + SD; time from NTP (no RTC chip) |

## Quality
- Unit tests for all DSP code: `pio test -e native`.
- Code review (`/code-review`) before each commit; security review once
  networking (WiFi, web access to recordings) is added.
- One git commit per finished step.
