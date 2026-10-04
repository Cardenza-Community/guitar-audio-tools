# Runtime Cardenza support

Based on preserved upstream `1862a7acb3b8e01cf8417ef0eee4c16c3ddfa486`. Default `unified` and CI-compatible alias `cardenza` use the exact M5Unified fork `203Null/M5Unified@74fe31c6d9a2bd7c04f81eb4f8f0af99262e3bc2` (upstream0.2.24), M5GFX0.2.31, M5Cardputer1.1.1. Hardware is selected at runtime using ES8156 identity; original Cardputer keyboard identity is preserved. The fork owns LED hold, no Cardenza battery ADC/RGB and codec initialization. No app-local hardware HAL or Power/RGB linker wrappers remain.

Preserved board abstraction, M5StickS3, newer recorder/storage/UI, main-loop external-power policy and battery UI on physical battery boards. ES8311 gain/callback on ADV/Stick; original/Cardenza PDM uses digital gain. Fork restores DAC after microphone handoff.

The independent `LAUNCHER_NVS_GUARD` and erase wrapper preserve shared Launcher NVS full-partition recovery; normal page GC and app/filesystem erase continue. The actual existing host guard test passes. Existing workflow and prepare.py bytes were preserved; prepare.py generated the raw app image and original dependency licensing notices successfully.

Final publishing-tree build PASS (`pio run -e cardenza`), 768592 bytes, SHA-256 `b85cea0311292864b8a2fd9b435374307bf3e6c04a6283551729c9815a8906f4`. This image was not flashed or physically validated. Historical device results for earlier images do not prove this image's hardware behavior.

Native DSP tests:83/83 PASS. M5StickS3 source/target retained; its build and hardware were not tested in this change.
