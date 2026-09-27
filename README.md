# MANTIS STUDIO — M5Stack Core2

A tiny, performance-first music workstation: drums, lead synth, bass synth, an audio track, a mixer, and a dancing
mantis — on one Core2. Sister project of **SYNAPSE** (the visual / fidget firmware); each has its own repo and web flasher.

## Build & flash
* **GitHub:** push (or drop a zip of this folder in the repo root). `.github/workflows/ci.yml` builds with PlatformIO,
  merges the bootloader, partitions and app into `firmware-merged.bin`, and publishes `website/` to GitHub Pages with a
  one-click web installer (Chrome / Edge).
* **Local:** `pio run -e m5stack-core2 -t upload`.

## Controls
| | |
|---|---|
| **A / C** | previous / next screen: DRUM · LEAD · BASS · AUDIO · MIX · MANTIS |
| **B** | context: record if a pad or the audio track is armed, otherwise play / stop (always from bar 1) |
| **hold B** | tap-tempo overlay with an audible click; tap anywhere, B when done |
| **REC chip** | whether playing on this screen is recorded into the loop (overdub, quantised to 1/32) |
| **hold CLR** | clear this screen's track (1 s hold, progress shown) |

The header is the always-visible beat indicator: it flashes on every beat (strongest on the one), shows bar.beat and
four beat dots, and a line under it shows where you are in the 4-bar loop.

### DRUM
Hat, open hat (choked by the closed hat), kick, snare. Hits while playing are recorded. Each pad shows its 4 bars as dots.
Hold a pad 1 s to arm it (blinks yellow), press **B** to sample: 3-beat haptic count-in, 0.8 s capture, auto-trimmed and
normalised. Tap an armed pad again to restore its factory sound.

### LEAD
16 scale-locked keys (2 octaves), two fingers at once, slide to change notes. Chips: **key**, **scale**
(major, minor, dorian, pentatonic, blues), **play** (note → chord → arp: held keys arpeggiate in 16ths, locked to the grid),
**sound** (pluck, pad, chip). **Tilt** the Core2 left/right to brighten or darken the filter. The strip at the bottom is
the piano roll of the lead track with the playhead.

### BASS
Mono bass with glide when you play legato. Chips: key, scale (shared with the lead), **oct** 1–3, **sound**
(electro: resonant squelch, chip: 25 % pulse, dub: sine + sub).

### AUDIO
One clip of 1, 2 or 4 bars (**bars** chip), always starting on bar 1 of the loop. Tap the red button to arm, **B** to
record: one bar count-in, then capture. **mute** chip, hold **CLR** to delete. Changing the tempo later doesn't stretch
the clip.

### MIX
Faders and meters for DRUM, LEAD, BASS, AUDIO and MASTER. Tap a name to mute, hold it 1 s to clear that track.
**slot** 1–4, **hold SAVE** / **hold LOAD**, and **− / +** for fine tempo.

### MANTIS
The Synapse mantis dances to your song on the demoscene stage, locked to the sequencer's beat. Tap to pet it.

## Recording and the speaker
The Core2 routes its microphone and speaker through one I2S port, so they can't run at the same time. While a sample or a
clip records, playback pauses, and the **vibration motor becomes the metronome** (strong on the one, lighter on the other
beats) after a count-in. Playback resumes from bar 1 afterwards, with the clip in place.

## SD card
```
/MANTIS/PROJECTS/<1-4>/song.bin      tempo, patterns, notes, sounds, mixer
/MANTIS/PROJECTS/<1-4>/pad<0-3>.raw  sampled pads (16 kHz mono s16)
/MANTIS/PROJECTS/<1-4>/clip.raw      audio track
/MANTIS/SAMPLES/{kick,snare,hat_closed,hat_open}.raw   optional replacement kit, loaded at boot
```
Without a card everything works except save / load.

## How it works
| file | role |
|---|---|
| `src/audio.cpp` | streaming mixer: every voice rendered in 192-sample blocks at 22.05 kHz on core 0 and queued to the speaker (≈17 ms buffer); sample-accurate sequencer inside the render; 6-voice lead, mono bass with SVF, drum voices, clip, click, soft clip; recording, quantising with latency compensation, arp; sampler / clip state machines; SD projects |
| `src/main.cpp` | UI, multi-touch keyboards, screens, double-buffered display pushed from core 0 |
| `src/mantis.cpp`, `mantis_rig.h` | the dancing mantis (shared with Synapse) |
| `src/fx.cpp` | 160×120 indexed demo engine with dual-core raster (stage behind the mantis) |

Tuning knobs: `BLK` and `LAT_SAMPLES` in `audio.cpp` (latency vs. safety), speaker `dma_buf_len / dma_buf_count` in `aud::begin()`.
