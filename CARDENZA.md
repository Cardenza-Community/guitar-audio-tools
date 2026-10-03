# Cardenza support

Build `pio run -e cardenza -j4` with the existing pioarduino 54.03.20 platform (Arduino 3.2.0/IDF5.4.1). PDM microphone CLK43/DATA46 is preserved for tuner, recording and analysis. Speaker playback and microphone capture share GPIO43: existing app lifecycle stops one before starting the other. Playback callback restores the ES8156 32fs format through the owning M5 external I2C driver, rather than bitbanging a bus already in use.

The PDM microphone has no ES8311 analog PGA: selected microphone gain is applied digitally with saturating samples, and Mic Test labels it Digital. Digital gain also amplifies microphone noise; dB SPL calibration remains device/microphone dependent. Recorded WAV and metronome playback use the ES8156 DAC. ADC-specific register writes are disabled on Cardenza.

Hardware: original Cardputer V1 matrix keyboard/display/SD; ES8156 DAC at I2C 0x08 (SDA2/SCL1); stereo Philips I2S, 16-bit slots, BCLK41/LRCK43/DOUT42. The shared MIT header is vendored from `../shared/cardenza_hal.h` with its license. It verifies codec identity/registers before peripheral setup and holds keyboard LED EN21 high. GPIO38 remains LCD backlight. No gyro, battery ADC, charging management or WS2812 output is enabled for this target. Buffers use internal RAM; no PSRAM is assumed.

These are app-only images for installation through the Cardenza Launcher. Do not flash a project-generated partition table/bootloader over the Launcher. Upstream targets retain their original behavior. Compilation and source review are separate from physical UI, audio, microphone and SD validation; verify these on target hardware before a release.

M5Unified 0.2.22 is pinned (separate translation units). Two linker wrappers suppress Power.begin and RGB setup. A newer unity-build M5Unified cannot use this wrapper technique safely. Verify the final ELF has only `__wrap__ZN2m511Power_Class5beginEv` and `__wrap__ZN2m59M5Unified10_setup_ledEN4lgfx6boards7board_tE`, without their original definitions. This matters because GPIO10 is a USB switch control on Cardenza, not a battery ADC. PlatformIO may retain both a transitive latest library and the exact pinned version; the build graph/compiled path and ELF must match the pinned version.

Upstream M5StickS3 support is retained. The Cardenza initialization lives in the
upstream board layer, and its battery display does not read the absent battery
ADC or charging circuit.

The Cardenza build rejects full DATA/NVS partition erases, including Arduino
startup recovery that would clear the Launcher's shared settings. Normal NVS
writes and erases of other explicitly selected partitions are unaffected. An
NVS recovery error requires deliberate repair rather than automatic deletion.
