# ST Sat — guide for agents

ST Sat is the **reference plugin** for the ST family and is built on the shared module in `../st-shell`
(https://github.com/davidstokarski/st-shell). Before changing anything, read the shell docs:

- `../st-shell/CLAUDE.md` — non-negotiables (design, real-time safety, native menus, verification)
- `../st-shell/docs/DESIGN.md` — the visual language this plugin defines
- `../st-shell/docs/ARCHITECTURE.md` — processor / editor / threading patterns
- `../st-shell/docs/EXAMPLES.md` — map of where each pattern lives in this repo

Plugin-specific notes:

- Parameters: `Source/Params.*` (IDs are permanent — never rename them). Per-band IDs are `b<n>_<suffix>`.
- DSP: `Source/dsp/Engine.*` (oversampling → crossover → bands → Space → sum). Saturation styles in
  `Source/dsp/Saturation*`; if a style's processing changes, regenerate `SaturationCalibration.h`
  (`StSatDspTests --calibrate --write`).
- UI: `Source/ui/MultibandDisplay.*` (display), `Source/ui/Panels.*` (band bar, popovers, footer), `Source/PluginEditor.*`.
- Verify: `StSatPluginTests` (stress), `StSatPluginTests --snapshot <dir>` (look at the PNGs), `auval -v aufx Stst StAu`.
  Website videos: `StSatPluginTests --video <dir> [scene ...]` (`tests/VideoRender.cpp`, on the shell's
  `st_shell/testing/st_video.h`): scenes drum-bus, vocal-space.
- Release: `scripts/release.sh` (universal build → GitHub release zip with the .vst3 and .component).
- Anything reusable belongs in st-shell, not here.
- Multiview: `Source/ui/Strip.*` (`SatStrip`, ST Sat's module in the shell's `st::MultiView`, returned by
  `StSatProcessor::createStripView`). Other ST plug-ins (ST EQ) render it off screen and forward the mouse, so it
  uses knobs only. The editor opens the multiview from the header button.
