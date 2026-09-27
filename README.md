# MANTIS STUDIO

A tiny, performance-first music workstation for the M5Stack Core2.

**Design target:** iMaschine-like immediacy and musical power, rebuilt as a praying-mantis instrument rather than a miniature desktop DAW.

## Tracks

- DRUM — four-pad sample/synth drum machine with overdub loop recording
- LEAD — chord/scale/arpeggio synth with loop recording
- BASS — electro / chip / dub bass synth with loop recording
- AUDIO — one microphone/audio clip track, stored on SD
- MIX — four-track mixer and master transport

All sequenced instruments share one musical transport. Event tracks are an internal, deliberately tiny MIDI-like representation; there is no user-facing MIDI editor.

## Hardware

- M5Stack Core2
- internal display/touch/buttons
- internal speaker
- internal microphone
- internal haptic motor
- internal IMU
- microSD
- battery operation

## Build

The repository follows the Mantis zip-first GitHub Actions + GitHub Pages web-flasher template.

```bash
pio run -e m5stack-core2
pio run -e m5stack-core2 -t upload
```

The Pages site installs the merged image with `esp-web-install-button`.

## Storage

Projects live under `/MANTIS/PROJECTS/<name>/` on SD. Factory/demo assets live under `/MANTIS/SAMPLES/`.

Audio is intentionally constrained to embedded-friendly formats and durations. Sequenced tracks store compact events instead of rendered audio.

## Controls

Physical A/C move between the six work screens. B is the context action. Touch is the performance surface. The transport is always visible and the visual beat indicator is intentionally unmistakable whenever it is running.

The UI avoids deep menus: repeated taps cycle compact choices; holds expose secondary actions.
