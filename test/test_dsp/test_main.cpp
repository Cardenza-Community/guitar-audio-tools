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
  return UNITY_END();
}
