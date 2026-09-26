# Audiotools for Cardputer ADV

Audio tools for the M5Stack Cardputer ADV (ESP32-S3): sound level meter,
noise analysis, spectrum/spectrogram and an instrument tuner.

## Apps (planned)
1. **Decibel meter** – calibrated dB SPL, max, average (Leq), A-weighting
2. **Guitar tuner** – needle gauge ±50 cents, note name
3. **Polyphonic tuner** – strum all strings at once (like PolyTune)
4. **Spectrum analyser** – green/yellow/red bars reacting to music
5. **BPM detector** – from music or by tapping (tap tempo)
6. **Vocal intonation trainer** – deviation from the target note in cents
7. **Recorder** – WAV files on the SD card
8. **Sound monitor** – night/snoring log, long-term level graph, sound alarm

Design and development order: [ARCHITECTURE.md](ARCHITECTURE.md).
Current state: skeleton (launcher, gapless audio input, app interface, PC tests)
with *Decibel meter* and *Mic test*. The other apps show as "coming soon" in the launcher.

## Controls
- Launcher: `,` / `/` browse the apps, `Enter` opens one, `Esc` (top left key) returns to the launcher.
- Decibel meter: `a` A/Z weighting, `s` Fast/Slow, `r` reset Leq/Max/Min,
  `c` calibration (`;`/`.` ±0.5 dB, `,`/`/` ±5 dB, `Enter` saves, `c` cancels).
  Samples at 32 kHz; A-weighting follows IEC 61672 within 0.25 dB up to 8 kHz.
  Automatic range: codec gain 18 dB, switches to 0 dB for very loud sound.
- Mic test: `;` / `.` change the analog microphone gain (0–30 dB in 3 dB steps),
  `g` runs an automatic gain test (play a steady tone; the level should rise
  6 dB per step). The serial console (115200 baud) prints the values 4× per second.

## Files
| Path | Content |
|------|---------|
| `src/main.cpp` | starting apps, feeding them audio, keys and redraws |
| `src/launcher.cpp` | app carousel with icons, list of all apps |
| `src/app.h` | interface every app implements |
| `src/apps/` | the apps (`mic_test.cpp`, ...) |
| `src/services/` | `audio_in` (gapless microphone stream), `settings` (NVS), `ui` (screen helpers) |
| `src/hw/es8311.*` | direct access to the ES8311 codec (gain, register dump) |
| `lib/dsp/` | signal processing without hardware: levels, YIN pitch, notes |
| `test/test_dsp/` | unit tests of `lib/dsp`, run on the PC |

## Building
PlatformIO: `pio run`, upload with `pio run -t upload`,
unit tests on the PC with `pio test -e native` (needs gcc).

**The Arduino core must be 3.2.0** (pinned in `platformio.ini` through the
pioarduino platform 54.03.20). With core 3.3.x (ESP-IDF 5.5) the ADV microphone
returns only constant samples (-1), see
[espressif/esp-idf#18621](https://github.com/espressif/esp-idf/issues/18621).
Credit for finding this goes to the
[audio-spy](https://github.com/Tombotronic/audio-spy) project.

## Notes on the ADV microphone (ES8311)
Unlike the original Cardputer (SPM1423 PDM microphone wired straight to I2S),
the ADV routes the microphone through the **ES8311** codec (I2C address 0x18).
Firmware written for the original Cardputer does not set the codec up, so the
microphone stays silent on the ADV.

M5Unified 0.2.23 powers the codec up with PGA 0 dB (reg 0x14 = 0x10),
ADC volume 0 dB (0x17 = 0xBF), equalizer bypassed and dynamic high-pass filter
on (0x1C = 0x6A). It then multiplies the samples in software
(`magnification` 16 / (`over_sampling` 2 × 2) = ×4). This project instead
raises the analog gain in the codec and takes the samples unscaled
(`magnification` 4, `over_sampling` 2 → ×1).

Useful registers (ES8311 User Guide Rev 1.11):

| Reg | Meaning |
|-----|---------|
| 0x14 | bits 5:4 input (1 = MIC1), bits 3:0 PGA gain 0–30 dB in 3 dB steps |
| 0x16 | ADC_SCALE: digital gain 0–42 dB in 6 dB steps |
| 0x17 | ADC volume −95.5 to +32 dB in 0.5 dB steps (0xBF = 0 dB) |
| 0x18–0x19 | ALC (automatic level control) – must stay off for measurements |
| 0x1A | automute / noise gate – off for measurements |
| 0x1B, 0x1C | high-pass filter (DC removal), equalizer bypass |
| 0x1D–0x30 | equalizer coefficients (one 2nd-order biquad) |

The microphone needs about 1 s to warm up after power-on (returns zeros).
The microphone and the speaker share the I2S bus, so the speaker is turned
off before recording.

## License
MIT, see [LICENSE](LICENSE).
