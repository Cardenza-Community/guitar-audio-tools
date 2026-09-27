# Audiotools for Cardputer ADV

Audio tools for the M5Stack Cardputer ADV (ESP32-S3): sound level meter,
noise analysis, spectrum/spectrogram and an instrument tuner.

## Apps (planned)
1. **Decibel meter** – calibrated dB SPL, max, average (Leq), A-weighting
2. **Guitar tuner** – needle gauge ±50 cents, note name
3. **Polyphonic tuner** – strum all strings at once (like PolyTune)
4. **Spectrum analyser** – green/yellow/red bars reacting to music
5. **BPM detector** – from music or by tapping (tap tempo)
6. **Metronome** – for musicians (replaces the vocal intonation trainer idea)
7. **Recorder** – WAV files on the SD card
8. **Sound monitor** – night/snoring log, long-term level graph, sound alarm

Design and development order: [ARCHITECTURE.md](ARCHITECTURE.md).
Current state: skeleton (launcher, gapless audio input, app interface, PC tests)
with *Decibel meter*, *Guitar tuner*, *PolyTune*, *Spectrum*, *BPM* and *Mic test*. The other apps show as "coming soon" in the launcher.

## Controls
Every app: `h` shows a help page with its keys (big, readable font), `Esc` goes
back to the launcher. The keys follow one scheme: `Enter` = the main action,
`,` / `/` (left/right) = change a mode or value, `;` / `.` (up/down) = microphone
gain (shown for 1.5 s as "MIC GAIN 27 dB", remembered per app). All text is white or coloured (no grey text: it is hard to read on the
small display).

- Launcher: `,` / `/` browse the apps, `Enter` opens one.
- Decibel meter: `a` A/Z weighting, `s` Fast/Slow, `Enter` (or `r`) reset Leq/Max/Min,
  `c` calibration (`;`/`.` ±0.5 dB, `,`/`/` ±5 dB, `Enter` saves, `c` cancels).
  Samples at 32 kHz; A-weighting follows IEC 61672 within 0.25 dB up to 8 kHz.
  Automatic range: codec gain 18 dB, switches to 0 dB for very loud sound.
- Guitar tuner: `Enter` (or `m`) GUITAR / CHROMATIC mode, `,`/`/` reference pitch A4 −1/+1 Hz
  (430–450), `;`/`.` microphone gain. Needle ±50 cents, green centre ±3 cents.
  GUITAR mode always shows the nearest string of the standard tuning, even when
  it is more than a semitone off ("E −90, tune up"). Measured accuracy with
  tones from a laptop speaker (220/440/880 Hz): within ±0.15 cents.
  The needle moves only after 3 agreeing, clearly periodic readings (pluck noise
  is not shown); GUITAR mode searches 60–420 Hz only (no octave errors at the
  pluck); a sub-harmonic of the ringing note (low E resonating while the high E
  decays) is ignored; a new pluck (+6 dB) starts over. For 0.5 s after a
  pluck the needle is thin and light: the string starts sharp and settles
  (the low E by 20–35 cents in the first second). Below 100 Hz the needle is
  smoothed twice as strongly.
- PolyTune: strum all six open strings; after about 1.2 s one column per
  string shows the deviation (marker up = sharp, down = flat, green centre
  ±3 cents; an arrow above the column says which way to tune: yellow for
  10–50 cents, red for more than 50; "?" = string not heard). Weak strums,
  handling noise and steady background tones in the room are ignored.
  `;`/`.` microphone gain, `Enter` clears the result, `c` calibration.
  Accuracy on synthetic chords ±1.3 cents. In a real strum the low strings read
  flat compared with the single-string tuner (lighter pluck = less pitch glide,
  inharmonic wound strings): a default correction measured on an unplugged
  electric guitar is applied (E −23.6, A −8.5, D −4.4, G −1.2, B +5.7,
  E +1.7 cents). For another guitar or new strings: tune with the tuner,
  press `c`, strum 3 times (saved; `Enter` in that screen restores the default).
- Spectrum: 16 bars 60 Hz – 16 kHz of green/yellow/red blocks, FFT of 2048
  samples at 32 kHz every 32 ms, bars fall slowly, peaks hold 0.8 s. Automatic
  sensitivity: the scale jumps to the loudest band and recovers 6 dB/s.
  `Enter` (or `p`) peaks on/off, `,` / `/` (or `r`) bar range 20 / 30 / 40 dB.
- BPM: AUTO listens to music (spectral-flux onsets in ~20 bands, a pulse comb
  with the half/double tempo "family" chooses the beat, autocorrelation over
  2–4 beats refines it; first reading after ~3 s, then twice per second over up
  to 6 s). The shown tempo keeps its octave; a new tempo must last 1.5 s,
  2/3 or 3/2 of the shown one 3 s. Grey + "uncertain" when the rhythm is not
  clear. `Enter` or space switches to tapping at once (tempo after 3 taps,
  exactly as tapped); 3 s without a tap: listening again. A dot flashes on the
  beat. `,` /2, `/` x2, `r` restart, `d` dump onset data (serial).
  Tested on songs played from a phone: Sandstorm 136.1, Levels 126.0, Bad
  Romance 119, Thunderstruck 133, Another One Bites the Dust 110.
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
| `lib/dsp/` | signal processing without hardware: levels, weighting, YIN pitch, notes, tuning, PolyTune, FFT, spectrum bands, onsets, tempo |
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

### Pitch accuracy
Plain YIN estimates the fraction of the period with a parabola through three
points. With real sound (harmonics) that is off by up to ~0.07 samples – 3.4
cents for 440 Hz at 16 kHz. `lib/dsp/pitch.cpp` therefore measures the fraction
again over many periods (about 520 samples), which divides the error by the
number of periods. `test/test_dsp/recording_440.h` is a real recording that
guards this (plain YIN reads it +3.35 cents sharp).

The microphone needs about 1 s to warm up after power-on (returns zeros).
The microphone and the speaker share the I2S bus, so the speaker is turned
off before recording.

## License
MIT, see [LICENSE](LICENSE).
