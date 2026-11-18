# ST Sat

Multiband saturation with upward + downward dynamics. VST3 and AU, macOS.
Built on [st-shell](https://github.com/davidstokarski/st-shell).

## Features
- Up to 6 bands, phase-coherent Linkwitz–Riley crossovers (12/24/48 dB/oct), draggable in the display
- 26 saturation styles across Tube, Tape, Saturation, Amp, Clip and FX categories
- Per band: Drive, Dynamics (upward + downward compression / expansion with full threshold, ratio, attack,
  release and pre/post control), Tone tilt, resonant Feedback + frequency, Mix, Level, Pan,
  Stereo/Mid/Side/Left/Right processing, solo / mute / bypass
- Global input, output, mix (latency-compensated), pan, auto gain, oversampling 1x–8x
- Modulation: LFOs (tempo sync), envelope followers (input or any band), macros, drag-and-drop to any knob
- Spectrum analyser (pre/post), presets, A/B compare, undo/redo, resizable UI

## Build

Expects `../st-shell` next to this repo (otherwise it is fetched from GitHub) and JUCE 9.0.5
in `../.st-deps/JUCE-9.0.5` (otherwise fetched).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Built plugins are copied to `~/Library/Audio/Plug-Ins/{VST3,Components}`.
Validate the AU with `auval -v aufx Stst StAu`.

## Usage tips
- Double-click the display to split a band; drag dividers to move crossovers; drag a band vertically to set Drive.
- Hover a band pill and click × to remove it.
- Drag a modulation source's handle onto any knob to modulate it.
- Alt-click or double-click resets a knob; cmd-click types a value; shift-drag for fine control.
