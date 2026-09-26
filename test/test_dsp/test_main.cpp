// Unit tests for the DSP library, run on the PC: pio test -e native
// Every test builds a synthetic signal with a known answer.
#include <unity.h>
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
  RUN_TEST(test_tuner_real_decaying_high_e);
  return UNITY_END();
}
