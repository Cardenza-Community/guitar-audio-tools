// Unit tests for the DSP library, run on the PC: pio test -e native
// Every test builds a synthetic signal with a known answer.
#include <unity.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "level.h"
#include "notes.h"
#include "pitch.h"
#include "sound_level.h"
#include "tuning.h"
#include "recording_440.h"
#include "decaying_high_e.h"
#include "decimator.h"
#include "polytune.h"
#include "strums.h"
#include "fft.h"
#include "spectrum.h"
#include "auto_range.h"
#include "onset.h"
#include "tempo.h"
#include "songs.h"
#include <complex>
#include "weighting.h"

static const int RATE = 16000;
static const float PI_F = 3.14159265f;

// Sine wave (plus optional harmonics) as 16-bit samples.
static std::vector<int16_t> tone(float hz, float amplitude, size_t count,
                                 std::vector<float> harmonics = {}) {
  std::vector<int16_t> out(count);
  for (size_t i = 0; i < count; i++) {
    float t = (float)i / RATE;
    float v = std::sin(2 * PI_F * hz * t);
    for (size_t h = 0; h < harmonics.size(); h++)
      v += harmonics[h] * std::sin(2 * PI_F * hz * (h + 2) * t);
    out[i] = (int16_t)std::lround(amplitude * v);
  }
  return out;
}

static std::vector<float> toFloat(const std::vector<int16_t> &x) {
  std::vector<float> out(x.size());
  dsp::removeDc(x.data(), out.data(), x.size());
  return out;
}

static float detectPitch(float hz, std::vector<float> harmonics = {}) {
  dsp::YinDetector yin(RATE, 60, 1000, 1024);
  auto signal = toFloat(tone(hz, 8000, yin.samplesNeeded(), harmonics));
  return yin.detect(signal.data(), signal.size());
}

// ---------- level ----------

void test_full_scale_sine_is_minus_3_dbfs() {
  auto x = toFloat(tone(1000, 32767, 16000));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -3.01f, dsp::toDbfs(dsp::rms(x.data(), x.size())));
}

void test_half_amplitude_is_6_db_lower() {
  auto x = toFloat(tone(1000, 16384, 16000));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -9.03f, dsp::toDbfs(dsp::rms(x.data(), x.size())));
}

void test_silence_is_silence_db() {
  std::vector<float> x(1000, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(dsp::SILENCE_DB, dsp::toDbfs(dsp::rms(x.data(), x.size())));
}

void test_dc_offset_is_removed() {
  std::vector<int16_t> x(100, 500);
  std::vector<float> y(100);
  dsp::removeDc(x.data(), y.data(), x.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, dsp::rms(y.data(), y.size()));
}

void test_peak() {
  auto x = tone(440, 12345, 1000);
  TEST_ASSERT_INT_WITHIN(1, 12345, dsp::peakAbs(x.data(), x.size()));
}

// ---------- pitch ----------

void test_pitch_a4() { TEST_ASSERT_FLOAT_WITHIN(0.05f, 440.0f, detectPitch(440.0f)); }

void test_pitch_high_notes() {
  // few samples per period: 0.25 cent tolerance
  const float notes[] = {523.25f, 659.26f, 880.0f};
  for (float hz : notes) TEST_ASSERT_FLOAT_WITHIN(hz * 0.00015f, hz, detectPitch(hz));
}

void test_pitch_guitar_strings() {
  // standard tuning E2 A2 D3 G3 B3 E4
  const float strings[] = {82.41f, 110.00f, 146.83f, 196.00f, 246.94f, 329.63f};
  for (float hz : strings) TEST_ASSERT_FLOAT_WITHIN(0.1f, hz, detectPitch(hz));
}

void test_pitch_with_strong_harmonics() {
  // a plucked string has strong overtones; YIN must still find the fundamental
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 110.0f, detectPitch(110.0f, {0.9f, 0.7f, 0.5f}));
}

void test_pitch_resolution_is_below_one_cent() {
  // 1 cent at 110 Hz is 0.064 Hz
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 110.5f, detectPitch(110.5f));
}

void test_noise_has_no_pitch() {
  dsp::YinDetector yin(RATE, 60, 1000, 1024);
  std::vector<float> noise(yin.samplesNeeded());
  uint32_t seed = 12345;
  for (auto &v : noise) {
    seed = seed * 1103515245u + 12345u;  // simple deterministic random numbers
    v = (float)((int32_t)(seed >> 16) % 8000);
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, yin.detect(noise.data(), noise.size()));
}

void test_too_few_samples_gives_zero() {
  dsp::YinDetector yin(RATE, 60, 1000, 1024);
  std::vector<float> x(100, 1.0f);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, yin.detect(x.data(), x.size()));
}

void test_pitch_real_recording() {
  // real microphone recording of a 440 Hz tone with strong harmonics
  const size_t n = sizeof(RECORDING_440) / sizeof(RECORDING_440[0]);
  std::vector<float> x(n);
  dsp::removeDc(RECORDING_440, x.data(), n);
  dsp::YinDetector yin(RATE, 60, 1100, 1024);
  TEST_ASSERT_TRUE(n >= yin.samplesNeeded());
  float hz = yin.detect(x.data(), n);
  float cents = 1200 * std::log2(hz / RECORDING_440_HZ);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, cents);
}

void test_pitch_high_note_with_harmonics() {
  // without the refinement over many periods this read +1 cent
  TEST_ASSERT_FLOAT_WITHIN(1046.5f * 0.0002f, 1046.5f, detectPitch(1046.5f, {0.35f, 0.2f}));
}

// ---------- notes ----------

void test_note_a4() {
  dsp::Note n = dsp::noteFromFrequency(440.0f);
  TEST_ASSERT_EQUAL_STRING("A", n.name);
  TEST_ASSERT_EQUAL(4, n.octave);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, n.cents);
}

void test_note_low_e_string() {
  dsp::Note n = dsp::noteFromFrequency(82.41f);
  TEST_ASSERT_EQUAL_STRING("E", n.name);
  TEST_ASSERT_EQUAL(2, n.octave);
  TEST_ASSERT_EQUAL(40, n.midi);
}

void test_cents_sharp_and_flat() {
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 19.56f, dsp::noteFromFrequency(445.0f).cents);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -19.78f, dsp::noteFromFrequency(435.0f).cents);
}

void test_other_reference_pitch() {
  // with A4 = 442 Hz, 442 Hz is exactly A4
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dsp::noteFromFrequency(442.0f, 442.0f).cents);
}

void test_frequency_of_midi() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 440.0f, dsp::frequencyOfMidi(69));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 82.41f, dsp::frequencyOfMidi(40));
}


// ---------- weighting and sound level ----------

static const float METER_RATE = 32000;

// Level in dB of a sine after the filter, relative to the input.
static float filterGainDb(dsp::Weighting w, float hz) {
  dsp::WeightingFilter f(w, METER_RATE);
  const int n = (int)METER_RATE;          // 1 s; the first half lets the filter settle
  double inPower = 0, outPower = 0;
  for (int i = 0; i < n; i++) {
    float x = std::sin(2 * PI_F * hz * i / METER_RATE);
    float y = f.process(x);
    if (i >= n / 2) {
      inPower += x * x;
      outPower += y * y;
    }
  }
  return (float)(10 * std::log10(outPower / inPower));
}

void test_a_weighting_formula_matches_iec_table() {
  // nominal values from IEC 61672-1; the table names rounded frequencies,
  // the exact ones are 1000 * 10^(k/10) Hz (31.5 means 31.62 Hz)
  const int k[] = {-15, -12, -9, -6, -3, 0, 3, 6, 9};
  const float db[] = {-39.4f, -26.2f, -16.1f, -8.6f, -3.2f, 0, 1.2f, 1.0f, -1.1f};
  for (int i = 0; i < 9; i++)
    TEST_ASSERT_FLOAT_WITHIN(0.1f, db[i], dsp::aWeightingDb(1000 * std::pow(10.0f, k[i] / 10.0f)));
}

void test_a_weighting_filter_follows_the_curve() {
  // class 1 meters allow about +-1 dB in this range; the filter stays within 0.25 dB
  const float hz[] = {31.5f, 63, 125, 250, 500, 1000, 2000, 4000, 8000};
  for (float f : hz) {
    char msg[32];
    snprintf(msg, sizeof(msg), "%.1f Hz", f);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.3f, dsp::aWeightingDb(f), filterGainDb(dsp::Weighting::A, f), msg);
  }
}

void test_z_weighting_is_flat() {
  const float hz[] = {31.5f, 100, 1000, 5000, 12000};
  for (float f : hz) TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, filterGainDb(dsp::Weighting::Z, f));
}

static std::vector<int16_t> sine32k(float hz, float amplitude, float seconds) {
  std::vector<int16_t> out((size_t)(seconds * METER_RATE));
  for (size_t i = 0; i < out.size(); i++)
    out[i] = (int16_t)std::lround(amplitude * std::sin(2 * PI_F * hz * i / METER_RATE));
  return out;
}

void test_meter_full_scale_1khz() {
  dsp::SoundLevelMeter m(METER_RATE, dsp::Weighting::A);
  // 6 s: Slow (1 s time constant) needs a few seconds to settle
  auto x = sine32k(1000, 32767, 6);
  m.process(x.data(), x.size());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -3.01f, m.fastDb());
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -3.01f, m.slowDb());
  TEST_ASSERT_FLOAT_WITHIN(0.2f, -3.01f, m.leqDb());
}

void test_meter_leq_averages_energy() {
  // 1 s at -20 dBFS and 1 s at -40 dBFS: Leq = 10 log10((0.01 + 0.0001) / 2) = -22.97 dB
  dsp::SoundLevelMeter m(METER_RATE, dsp::Weighting::Z);
  float a20 = 32768 * std::sqrt(2.0f) * 0.1f, a40 = 32768 * std::sqrt(2.0f) * 0.01f;
  auto loud = sine32k(1000, a20, 1), quiet = sine32k(1000, a40, 1);
  m.process(loud.data(), loud.size());
  m.process(quiet.data(), quiet.size());
  TEST_ASSERT_FLOAT_WITHIN(0.2f, -22.97f, m.leqDb());
  TEST_ASSERT_FLOAT_WITHIN(0.3f, -20.0f, m.maxDb());
  TEST_ASSERT_FLOAT_WITHIN(0.3f, -40.0f, m.fastDb());   // Fast has followed the quiet part
}

void test_meter_fast_is_faster_than_slow() {
  // after 0.25 s of a tone starting from silence Fast is nearly there, Slow is not
  dsp::SoundLevelMeter m(METER_RATE, dsp::Weighting::Z);
  auto x = sine32k(1000, 10000, 0.25f);
  m.process(x.data(), x.size());
  float target = dsp::toDbfs(10000 / std::sqrt(2.0f));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, target, m.fastDb());
  TEST_ASSERT_TRUE(m.slowDb() < target - 4);
}

// ---------- tuner ----------

void test_nearest_string_in_tune() {
  dsp::StringMatch m = dsp::nearestGuitarString(110.0f);
  TEST_ASSERT_EQUAL(1, m.index);                  // A string
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, m.cents);
}

void test_nearest_string_far_out_of_tune() {
  // low E almost a semitone flat: still the E string, not D#
  dsp::StringMatch m = dsp::nearestGuitarString(78.0f);
  TEST_ASSERT_EQUAL(0, m.index);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -95.1f, m.cents);
  // 120 Hz lies between A2 (110) and D3 (146.8): A +150.6 c is closer than D -349.2 c
  m = dsp::nearestGuitarString(120.0f);
  TEST_ASSERT_EQUAL(1, m.index);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 150.6f, m.cents);
}

void test_nearest_string_high_e_and_reference() {
  dsp::StringMatch m = dsp::nearestGuitarString(331.0f);
  TEST_ASSERT_EQUAL(5, m.index);
  TEST_ASSERT_EQUAL(64, m.midi);
  // with A4 = 442 Hz everything is 7.85 cents higher
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, dsp::nearestGuitarString(110.5f, 442.0f).cents);
}

void test_smoother_ignores_single_octave_jump() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(110.0f);
  float out = s.push(220.0f);                     // one wrong reading
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 110.0f, out);
}

void test_smoother_follows_new_note() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(110.0f);
  float out = 0;
  for (int i = 0; i < 3; i++) out = s.push(146.83f);   // majority of the last 5
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 146.83f, out);
}

void test_smoother_calms_small_changes() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(110.0f);
  // readings jump to +10 cents: the output moves there gradually
  float target = 110.0f * std::pow(2.0f, 10 / 1200.0f);
  float first = 0, out = 0;
  for (int i = 0; i < 20; i++) {
    out = s.push(target);
    if (i == 2) first = out;                      // median has switched by now
  }
  TEST_ASSERT_TRUE(first > 110.0f && first < target);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, target, out);
}

void test_smoother_silence() {
  dsp::PitchSmoother s(6);
  for (int i = 0; i < 5; i++) s.push(110.0f);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FLOAT_WITHIN(0.01f, 110.0f, s.push(0));  // short gap: hold
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(0));      // 6th miss: silence
}

void test_smoother_onset_needs_three_agreeing_readings() {
  dsp::PitchSmoother s;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(329.6f, 0.05f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(329.7f, 0.05f));
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 329.65f, s.push(329.6f, 0.05f));
}

void test_smoother_onset_ignores_pluck_noise() {
  // an octave error at the pluck (659 Hz) is never shown
  dsp::PitchSmoother s;
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(659.3f, 0.05f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(329.6f, 0.05f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(329.6f, 0.05f));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 329.6f, s.push(329.6f, 0.05f));
}

void test_smoother_onset_needs_clear_tone() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(329.6f, 0.13f));
}

void test_smoother_holds_note_over_sub_harmonic() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(329.6f, 0.05f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 329.6f, s.push(82.4f, 0.1f));    // E4 / 4
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 329.6f, s.push(164.8f, 0.1f));   // E4 / 2
}

void test_smoother_new_pluck_starts_over() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 6; i++) s.push(329.6f, 0.05f, -50);          // high E rings quietly
  // the low E is plucked: 20 dB louder, a new note even though it is E4 / 4
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(82.41f, 0.05f, -30));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.push(82.41f, 0.05f, -30));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 82.41f, s.push(82.41f, 0.05f, -31));
}

void test_smoother_faded_note_keeps_its_echo() {
  // high E fades out (6 readings without pitch), then the resonating low E
  // (E4 / 4) is still heard: it must not show up as E2
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(329.2f, 0.05f, -55);
  for (int i = 0; i < 6; i++) s.push(0, 1, -58);
  TEST_ASSERT_FALSE(s.locked());
  float out = 0;
  for (int i = 0; i < 3; i++) out = s.push(82.3f, 0.09f, -59);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 329.2f, out);
}

void test_smoother_new_pluck_after_fade_is_new_note() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 5; i++) s.push(329.2f, 0.05f, -55);
  for (int i = 0; i < 6; i++) s.push(0, 1, -60);
  // the low E is plucked (much louder): a real E2
  s.push(82.41f, 0.05f, -40);
  s.push(82.41f, 0.05f, -40);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 82.41f, s.push(82.41f, 0.05f, -41));
}

void test_smoother_counts_notes_and_calms_low_e_more() {
  dsp::PitchSmoother s;
  for (int i = 0; i < 3; i++) s.push(82.41f, 0.05f, -40);
  TEST_ASSERT_EQUAL(1u, s.notes());
  // a +20 cent reading moves the low E only 15 % of the way (3 cents)
  float out = s.push(82.41f * std::pow(2.0f, 20 / 1200.0f), 0.05f, -40);
  out = s.push(82.41f * std::pow(2.0f, 20 / 1200.0f), 0.05f, -40);
  out = s.push(82.41f * std::pow(2.0f, 20 / 1200.0f), 0.05f, -40);   // median switched now
  float cents = 1200 * std::log2(out / 82.41f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 3.0f, cents);
}

void test_tuner_real_decaying_high_e() {
  // replay a real recording the way the tuner does (a reading every 512 samples);
  // the note was locked on E4 before this excerpt
  const size_t n = sizeof(DECAYING_HIGH_E) / sizeof(DECAYING_HIGH_E[0]);
  dsp::YinDetector yin(RATE, 60, 420, 1024);
  dsp::PitchSmoother s;
  for (int i = 0; i < 3; i++) s.push(329.6f, 0.05f, -57);
  const size_t need = yin.samplesNeeded();
  std::vector<float> x(need);
  int readings = 0;
  for (size_t start = 0; start + need <= n; start += 512) {
    dsp::removeDc(DECAYING_HIGH_E + start, x.data(), need);
    float level = dsp::toDbfs(dsp::rms(x.data(), need));
    float hz = yin.detect(x.data(), need);
    float out = s.push(hz, yin.lastAperiodicity(), level);
    TEST_ASSERT_FLOAT_WITHIN(329.6f * 0.006f, 329.6f, out);        // +-10 cents, never E2
    readings++;
  }
  TEST_ASSERT_TRUE(readings >= 14);
}

// ---------- decimator and polyphonic tuner ----------

static float decimatedGainDb(float hz) {
  dsp::Decimator d(4);
  auto x = tone(hz, 10000, 16000);
  std::vector<float> y(x.size() / 4 + 1);
  size_t n = d.process(x.data(), x.size(), y.data());
  // skip the start while the filter fills up
  double p = 0;
  for (size_t i = n / 2; i < n; i++) p += (double)y[i] * y[i];
  return (float)(10 * std::log10(p / (n - n / 2)) - 10 * std::log10(10000.0 * 10000 / 2));
}

void test_decimator_passes_guitar_range() {
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, decimatedGainDb(82.41f));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, decimatedGainDb(700));
}

void test_decimator_blocks_what_would_fold_down() {
  // 3700 Hz would appear as 300 Hz at 4 kHz
  TEST_ASSERT_TRUE(decimatedGainDb(3700) < -60);
}

// A strummed chord at 4 kHz: each string with harmonics (a little sharp, like
// real strings), its own detune and loudness, plus some noise.
static std::vector<float> chord(const float cents[6], const float amps[6], float noise = 0) {
  const float fs = 4000;
  std::vector<float> x(4096, 0.0f);
  uint32_t seed = 777;
  for (size_t i = 0; i < x.size(); i++) {
    float t = i / fs;
    float v = 0;
    for (int s = 0; s < 6; s++) {
      float f = dsp::frequencyOfMidi(dsp::GUITAR_MIDI[s]) * std::pow(2.0f, cents[s] / 1200);
      float decay = std::exp(-t * (0.5f + s * 0.4f));
      for (int k = 1; k <= 5; k++) {
        float stretch = 1 + 0.00005f * k * k;         // inharmonicity
        float fk = f * k * stretch;
        if (fk < fs / 2) v += amps[s] * decay * std::sin(2 * PI_F * fk * t + s + k) / k;
      }
    }
    seed = seed * 1103515245u + 12345u;
    v += noise * (((int32_t)(seed >> 16) % 2000) / 1000.0f - 1);
    x[i] = v;
  }
  return x;
}

void test_polytune_chord_in_tune() {
  const float cents[6] = {0, 0, 0, 0, 0, 0};
  const float amps[6] = {3000, 2500, 2500, 2000, 1500, 1500};
  auto x = chord(cents, amps, 50);
  dsp::PolyTuner p(4000, 4096);
  dsp::StringReading r[6];
  p.analyse(x.data(), 440, r);
  for (int s = 0; s < 6; s++) {
    printf("in tune: string %d  %+.2f c  amp %.0f\n", 6 - s, r[s].cents, r[s].amplitude);
    TEST_ASSERT_TRUE(r[s].found);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 0.0f, r[s].cents);
  }
}

void test_polytune_chord_out_of_tune() {
  const float cents[6] = {-35, 12, -8, 27, -50, 18};
  const float amps[6] = {3000, 2500, 2500, 2000, 1500, 1500};
  auto x = chord(cents, amps, 50);
  dsp::PolyTuner p(4000, 4096);
  dsp::StringReading r[6];
  p.analyse(x.data(), 440, r);
  for (int s = 0; s < 6; s++) {
    printf("detuned: string %d  expected %+.0f  got %+.2f c\n", 6 - s, cents[s], r[s].cents);
    TEST_ASSERT_TRUE(r[s].found);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, cents[s], r[s].cents);
  }
}

void test_polytune_missing_string() {
  const float cents[6] = {0, 0, 0, 0, 0, 0};
  const float amps[6] = {3000, 2500, 0, 2000, 1500, 1500};   // D string not played
  auto x = chord(cents, amps, 50);
  dsp::PolyTuner p(4000, 4096);
  dsp::StringReading r[6];
  p.analyse(x.data(), 440, r);
  TEST_ASSERT_FALSE(r[2].found);
  TEST_ASSERT_TRUE(r[1].found);
  TEST_ASSERT_TRUE(r[3].found);
}

void test_polytune_noise_only() {
  const float cents[6] = {0, 0, 0, 0, 0, 0};
  const float amps[6] = {0, 0, 0, 0, 0, 0};
  auto x = chord(cents, amps, 1.0f);
  dsp::PolyTuner p(4000, 4096);
  dsp::StringReading r[6];
  p.analyse(x.data(), 440, r);
  for (int s = 0; s < 6; s++) TEST_ASSERT_FALSE(r[s].found);
}

static void analyseStrum(const int16_t *block, size_t n, dsp::StringReading r[6]) {
  std::vector<float> x(block + 600, block + n);    // skip the pluck noise, like the app
  dsp::PolyTuner p(4000, 4096);
  TEST_ASSERT_TRUE(x.size() >= 4096);
  p.analyse(x.data(), 440, r);
}

void test_polytune_ignores_steady_background_tone() {
  // a steady 256 Hz tone in the room (like the one heard in real recordings)
  // lies in the B string's band and is stronger than the B string itself
  const float cents[6] = {0, 0, 0, 0, -25, 0};
  const float amps[6] = {3000, 2500, 2500, 2000, 300, 1500};
  auto strum = chord(cents, amps, 20);
  std::vector<float> before(4096, 0.0f);
  uint32_t seed = 99;
  for (size_t i = 0; i < 4096; i++) {
    float hum = 400 * std::sin(2 * PI_F * 256.0f * i / 4000);
    seed = seed * 1103515245u + 12345u;
    before[i] = hum + 20 * (((int32_t)(seed >> 16) % 2000) / 1000.0f - 1);
    strum[i] += 400 * std::sin(2 * PI_F * 256.0f * (i + 4096) / 4000);
  }
  dsp::PolyTuner p(4000, 4096);
  dsp::StringReading r[6];
  p.setBackground(before.data(), 0, 440);
  p.analyse(strum.data(), 440, r);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -25.0f, r[4].cents);   // not +62 (the hum)
}

void test_polytune_real_good_strum() {
  dsp::StringReading r[6];
  analyseStrum(STRUM_GOOD, sizeof(STRUM_GOOD) / sizeof(STRUM_GOOD[0]), r);
  const float expected[6] = {1.9f, 7.9f, -1.2f, 6.5f, 1.4f, 7.2f};
  for (int s = 0; s < 6; s++) {
    TEST_ASSERT_TRUE(r[s].found);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expected[s], r[s].cents);
  }
}

void test_polytune_default_calibration_on_tuned_guitar() {
  dsp::StringReading r[6];
  analyseStrum(STRUM_TUNED, sizeof(STRUM_TUNED) / sizeof(STRUM_TUNED[0]), r);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, -22.8f, r[0].cents);           // before: low E reads flat
  dsp::applyCalibration(r, dsp::DEFAULT_CALIBRATION);
  for (int s = 0; s < 6; s++) {
    TEST_ASSERT_TRUE(r[s].found);
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 0.0f, r[s].cents);            // after: all in tune
  }
}

void test_polytune_real_weak_strum() {
  dsp::StringReading r[6];
  analyseStrum(STRUM_WEAK, sizeof(STRUM_WEAK) / sizeof(STRUM_WEAK[0]), r);
  // the app ignores a strum whose loudest string is below MIN_STRUM_AMPLITUDE
  float strongest = 0;
  for (int s = 0; s < 6; s++) strongest = std::max(strongest, r[s].amplitude);
  TEST_ASSERT_TRUE(strongest < dsp::MIN_STRUM_AMPLITUDE);
}

// ---------- FFT and spectrum bands ----------

void test_fft_matches_slow_dft() {
  const size_t n = 64;
  std::vector<std::complex<float>> x(n), slow(n);
  uint32_t seed = 5;
  for (auto &v : x) {
    seed = seed * 1103515245u + 12345u;
    v = {(float)((int32_t)(seed >> 16) % 1000), 0.0f};
  }
  for (size_t k = 0; k < n; k++) {                 // DFT straight from the definition
    std::complex<double> sum = 0;
    for (size_t i = 0; i < n; i++) sum += std::complex<double>(x[i]) * std::polar(1.0, -2 * 3.14159265358979 * k * i / n);
    slow[k] = std::complex<float>(sum);
  }
  dsp::Fft fft(n);
  fft.forward(x.data());
  for (size_t k = 0; k < n; k++) TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, std::abs(x[k] - slow[k]));
}

void test_spectrum_sine_lights_its_band() {
  dsp::SpectrumBands sb(32000, 2048, 16, 40, 16000);
  std::vector<int16_t> x(2048);
  for (size_t i = 0; i < x.size(); i++) x[i] = (int16_t)std::lround(32767 * std::sin(2 * PI_F * 1000 * i / 32000));
  std::vector<float> db(sb.bands());
  sb.analyse(x.data(), db.data());
  int loudest = (int)(std::max_element(db.begin(), db.end()) - db.begin());
  TEST_ASSERT_TRUE(sb.centreHz(loudest) / 1000 < 1.4f && 1000 / sb.centreHz(loudest) < 1.4f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.01f, db[loudest]);
  // bands two or more away are far below
  for (int b = 0; b < sb.bands(); b++)
    if (std::abs(b - loudest) >= 2) TEST_ASSERT_TRUE(db[b] < db[loudest] - 40);
}

void test_spectrum_low_and_high_tones() {
  dsp::SpectrumBands sb(32000, 2048, 16, 40, 16000);
  std::vector<float> db(sb.bands());
  for (float hz : {60.0f, 12000.0f}) {
    std::vector<int16_t> x(2048);
    for (size_t i = 0; i < x.size(); i++) x[i] = (int16_t)std::lround(10000 * std::sin(2 * PI_F * hz * i / 32000));
    sb.analyse(x.data(), db.data());
    int loudest = (int)(std::max_element(db.begin(), db.end()) - db.begin());
    TEST_ASSERT_TRUE(sb.centreHz(loudest) / hz < 1.6f && hz / sb.centreHz(loudest) < 1.6f);
  }
}

void test_spectrum_silence() {
  dsp::SpectrumBands sb(32000, 2048, 16, 40, 16000);
  std::vector<int16_t> x(2048, 0);
  std::vector<float> db(sb.bands());
  sb.analyse(x.data(), db.data());
  for (float v : db) TEST_ASSERT_EQUAL_FLOAT(dsp::SILENCE_DB, v);
}

// ---------- automatic sensitivity of the spectrum ----------

static void feed(dsp::AutoRange &a, float db, float seconds) {
  for (float t = 0; t < seconds; t += 0.032f) a.update(db, 0.032f);
}

void test_autorange_jumps_to_music() {
  dsp::AutoRange a(30, -50);
  feed(a, -70, 2);
  a.update(-30, 0.032f);
  TEST_ASSERT_EQUAL_FLOAT(-30.0f, a.top());
  TEST_ASSERT_EQUAL_FLOAT(-60.0f, a.bottom());
}

void test_autorange_comes_down_slowly() {
  dsp::AutoRange a(30, -50);
  feed(a, -20, 1);
  feed(a, -40, 1);                                // music gets 20 dB quieter
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -26.0f, a.top()); // 6 dB per second
}

void test_autorange_silence_floor() {
  dsp::AutoRange a(30, -50);
  feed(a, -80, 5);
  TEST_ASSERT_EQUAL_FLOAT(-50.0f, a.top());
}

void test_autorange_range_change() {
  dsp::AutoRange a(30, -50);
  feed(a, -30, 1);
  a.setRange(20);
  TEST_ASSERT_EQUAL_FLOAT(-50.0f, a.bottom());
}

// ---------- onsets and tempo ----------

// 16 kHz audio with hits at the given tempo. kind 0: metronome clicks;
// kind 1: drums (kick on every beat, snare on 2 and 4, hi-hat on eighths).
static std::vector<int16_t> rhythm(float bpm, float seconds, int kind, float noise = 200) {
  const int fs = 16000;
  std::vector<float> x((size_t)(seconds * fs), 0.0f);
  uint32_t seed = 42;
  auto rnd = [&]() {
    seed = seed * 1103515245u + 12345u;
    return ((int32_t)(seed >> 16) % 2000) / 1000.0f - 1;
  };
  auto hit = [&](float t, float freq, float amp, float decay, float noisy) {
    size_t start = (size_t)(t * fs);
    for (size_t i = 0; i < (size_t)(0.15f * fs) && start + i < x.size(); i++) {
      float tt = (float)i / fs;
      float env = amp * std::exp(-tt / decay);
      x[start + i] += env * ((1 - noisy) * std::sin(2 * PI_F * freq * tt) + noisy * rnd());
    }
  };
  float beat = 60 / bpm;
  for (int b = 0; b * beat < seconds; b++) {
    float t = b * beat + 0.05f;
    if (kind == 0) {
      hit(t, 1500, 12000, 0.01f, 0.3f);
    } else {
      hit(t, 60, 14000, 0.08f, 0.1f);                           // kick
      if (b % 2 == 1) hit(t, 200, 9000, 0.05f, 0.8f);           // snare
      hit(t, 8000, 3000, 0.02f, 1.0f);                          // hi-hat
      hit(t + beat / 2, 8000, 3000, 0.02f, 1.0f);               // hi-hat off-beat
    }
  }
  std::vector<int16_t> out(x.size());
  for (size_t i = 0; i < x.size(); i++)
    out[i] = (int16_t)std::max(-32767.0f, std::min(32767.0f, x[i] + noise * rnd()));
  return out;
}

static float detectBpm(const std::vector<int16_t> &audio, int *onsets = nullptr) {
  dsp::OnsetDetector od(16000);
  dsp::TempoEstimator te(od.frameRate());
  int hits = 0;
  for (size_t i = 0; i < audio.size(); i += 256) {
    size_t n = od.process(audio.data() + i, std::min<size_t>(256, audio.size() - i));
    for (size_t k = 0; k < n; k++) {
      te.push(od.value(k));
      if (od.isOnset(k)) hits++;
    }
  }
  if (onsets) *onsets = hits;
  return te.estimate().bpm;
}

void test_tempo_metronome() {
  for (float bpm : {90.0f, 120.0f, 150.0f}) {
    float got = detectBpm(rhythm(bpm, 10, 0));
    printf("metronome %.0f -> %.2f\n", bpm, got);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, bpm, got);
  }
}

void test_tempo_equal_clicks_prefer_about_120() {
  // equally strong clicks at 174 BPM are also a valid 87 BPM (every beat with
  // an extra click in between); like most BPM meters the tempo closer to
  // 120 wins (the app has /2 and x2 keys for the other reading)
  float got = detectBpm(rhythm(174, 10, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 87.0f, got);
}

void test_tempo_drums_with_eighth_hihats() {
  // the hi-hats repeat twice per beat; the tempo must still be the beat
  for (float bpm : {100.0f, 128.0f}) {
    float got = detectBpm(rhythm(bpm, 10, 1));
    printf("drums %.0f -> %.2f\n", bpm, got);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, bpm, got);
  }
}

void test_tempo_silence_is_not_a_tempo() {
  std::vector<int16_t> quiet = rhythm(120, 10, 0, 200);
  for (auto &v : quiet) v = (int16_t)(v / 400);    // only faint noise remains... and clicks
  std::vector<int16_t> noise(160000);
  uint32_t seed = 3;
  for (auto &v : noise) {
    seed = seed * 1103515245u + 12345u;
    v = (int16_t)((int32_t)(seed >> 16) % 400 - 200);
  }
  dsp::OnsetDetector od(16000);
  dsp::TempoEstimator te(od.frameRate());
  for (size_t i = 0; i < noise.size(); i += 256) {
    size_t n = od.process(noise.data() + i, 256);
    for (size_t k = 0; k < n; k++) te.push(od.value(k));
  }
  TEST_ASSERT_TRUE(te.estimate().confidence < 0.2f);
}

void test_tempo_first_reading_after_3_seconds() {
  float got = detectBpm(rhythm(120, 3.3f, 0));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 120.0f, got);
}

static float songBpm(const uint16_t *env, size_t n) {
  dsp::TempoEstimator te(125);
  for (size_t i = 0; i < n; i++) te.push(env[i] / 1000.0f);
  return te.estimate().bpm;
}

void test_tempo_real_songs() {
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 119.0f, songBpm(SONG_BAD_ROMANCE, sizeof(SONG_BAD_ROMANCE) / 2));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 133.0f, songBpm(SONG_THUNDERSTRUCK, sizeof(SONG_THUNDERSTRUCK) / 2));
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 110.0f, songBpm(SONG_ANOTHER_ONE, sizeof(SONG_ANOTHER_ONE) / 2));
}

void test_tempo_electronic_songs_exact() {
  // programmed tempo: within 0.3 BPM
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 136.0f, songBpm(SONG_SANDSTORM, sizeof(SONG_SANDSTORM) / 2));
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 126.0f, songBpm(SONG_LEVELS, sizeof(SONG_LEVELS) / 2));
}

void test_tempo_near_silence_stays_in_range() {
  dsp::TempoEstimator te(125);
  for (size_t i = 0; i < sizeof(GAP_BETWEEN_SONGS) / 2; i++) te.push(GAP_BETWEEN_SONGS[i] / 1000.0f);
  float bpm = te.estimate().bpm;
  TEST_ASSERT_TRUE(bpm == 0 || (bpm >= 59 && bpm <= 201));
}

void test_onsets_find_claps() {
  int hits = 0;
  detectBpm(rhythm(120, 5, 0), &hits);      // 10 clicks in 5 s
  TEST_ASSERT_INT_WITHIN(1, 10, hits);
}

void test_tap_tempo() {
  dsp::TapTempo t;
  float bpm = 0;
  for (int i = 0; i < 6; i++) bpm = t.tap(1000 + i * 500);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, bpm);
  // one tap missed (a 1 s gap): the median keeps 120
  bpm = t.tap(1000 + 5 * 500 + 1000);
  bpm = t.tap(1000 + 5 * 500 + 1500);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, bpm);
  // a pause of 3 s starts a new series
  t.tap(20000);
  TEST_ASSERT_EQUAL(1, t.taps());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_full_scale_sine_is_minus_3_dbfs);
  RUN_TEST(test_half_amplitude_is_6_db_lower);
  RUN_TEST(test_silence_is_silence_db);
  RUN_TEST(test_dc_offset_is_removed);
  RUN_TEST(test_peak);
  RUN_TEST(test_pitch_a4);
  RUN_TEST(test_pitch_high_notes);
  RUN_TEST(test_pitch_guitar_strings);
  RUN_TEST(test_pitch_with_strong_harmonics);
  RUN_TEST(test_pitch_real_recording);
  RUN_TEST(test_pitch_high_note_with_harmonics);
  RUN_TEST(test_pitch_resolution_is_below_one_cent);
  RUN_TEST(test_noise_has_no_pitch);
  RUN_TEST(test_too_few_samples_gives_zero);
  RUN_TEST(test_note_a4);
  RUN_TEST(test_note_low_e_string);
  RUN_TEST(test_cents_sharp_and_flat);
  RUN_TEST(test_other_reference_pitch);
  RUN_TEST(test_frequency_of_midi);
  RUN_TEST(test_a_weighting_formula_matches_iec_table);
  RUN_TEST(test_a_weighting_filter_follows_the_curve);
  RUN_TEST(test_z_weighting_is_flat);
  RUN_TEST(test_meter_full_scale_1khz);
  RUN_TEST(test_meter_leq_averages_energy);
  RUN_TEST(test_meter_fast_is_faster_than_slow);
  RUN_TEST(test_nearest_string_in_tune);
  RUN_TEST(test_nearest_string_far_out_of_tune);
  RUN_TEST(test_nearest_string_high_e_and_reference);
  RUN_TEST(test_smoother_ignores_single_octave_jump);
  RUN_TEST(test_smoother_follows_new_note);
  RUN_TEST(test_smoother_calms_small_changes);
  RUN_TEST(test_smoother_silence);
  RUN_TEST(test_smoother_onset_needs_three_agreeing_readings);
  RUN_TEST(test_smoother_onset_ignores_pluck_noise);
  RUN_TEST(test_smoother_onset_needs_clear_tone);
  RUN_TEST(test_smoother_holds_note_over_sub_harmonic);
  RUN_TEST(test_smoother_new_pluck_starts_over);
  RUN_TEST(test_smoother_faded_note_keeps_its_echo);
  RUN_TEST(test_smoother_new_pluck_after_fade_is_new_note);
  RUN_TEST(test_smoother_counts_notes_and_calms_low_e_more);
  RUN_TEST(test_tuner_real_decaying_high_e);
  RUN_TEST(test_decimator_passes_guitar_range);
  RUN_TEST(test_decimator_blocks_what_would_fold_down);
  RUN_TEST(test_polytune_chord_in_tune);
  RUN_TEST(test_polytune_chord_out_of_tune);
  RUN_TEST(test_polytune_missing_string);
  RUN_TEST(test_polytune_noise_only);
  RUN_TEST(test_polytune_ignores_steady_background_tone);
  RUN_TEST(test_polytune_real_good_strum);
  RUN_TEST(test_polytune_default_calibration_on_tuned_guitar);
  RUN_TEST(test_polytune_real_weak_strum);
  RUN_TEST(test_fft_matches_slow_dft);
  RUN_TEST(test_spectrum_sine_lights_its_band);
  RUN_TEST(test_spectrum_low_and_high_tones);
  RUN_TEST(test_spectrum_silence);
  RUN_TEST(test_autorange_jumps_to_music);
  RUN_TEST(test_autorange_comes_down_slowly);
  RUN_TEST(test_autorange_silence_floor);
  RUN_TEST(test_autorange_range_change);
  RUN_TEST(test_tempo_metronome);
  RUN_TEST(test_tempo_equal_clicks_prefer_about_120);
  RUN_TEST(test_tempo_drums_with_eighth_hihats);
  RUN_TEST(test_tempo_silence_is_not_a_tempo);
  RUN_TEST(test_tempo_first_reading_after_3_seconds);
  RUN_TEST(test_tempo_real_songs);
  RUN_TEST(test_tempo_electronic_songs_exact);
  RUN_TEST(test_tempo_near_silence_stays_in_range);
  RUN_TEST(test_onsets_find_claps);
  RUN_TEST(test_tap_tempo);
  return UNITY_END();
}
