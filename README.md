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

## Haptic subwoofer

The Core2 can't reproduce low bass through its little speaker, so the chassis plays it. The vibration motor is
an ERM (a spinning off-centre weight): its **spin rate is its vibration frequency**, and the spin rate follows the
motor-rail voltage. Mantis Studio drives that rail directly in millivolts (AXP192 LDO3 on Core2, AXP2101 DLDO1 on
Core2 v1.1) and plays every bass note as a rotor speed in the note's own pitch class, folded into the motor's range
by octaves, so what you feel (and the faint buzz you hear) is **in tune** with the song.

* Between the 100 mV rail steps it dithers (sigma-delta, 200 decisions a second); the rotor's inertia averages them
  into in-between speeds, for pitch within a few cents.
* Note-ons kick-start the rotor with a short overdrive so fast bass lines stay tight; legato glides slide the rotor;
  releases cut it. Kicks get a full-rail punch, then the drum's body on the lowest note the motor can hold.
* The audio engine timestamps every bass note and kick with the moment it will be *heard*, and the motor is started
  a few milliseconds early because the rotor has mass, so feel and sound land together.
* **MIX → sub:** off / kick / full. **MIX → hold TUNE:** leave the Core2 on a table for ~8 s; it spins the motor at
  every rail step and measures the real rotor speed with the IMU (accelerometer low-pass bypassed, ~3 kHz sampling,
  zero-crossing count). The result is saved and used from then on; until then a datasheet model is used.
  The panel shows the note the chassis is playing.

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


## Drum pads: step editor and samples

* **EDIT** (top of the DRUM screen) opens the step editor: one row per pad, 16 sixteenths per bar, tabs for bars 1-4.
  Tap a cell to add or remove a hit (you hear it as you place it); the playing step is outlined. EDIT again returns
  to the pads.
* **hold CLR** clears the drum *sequence* only. Samples stay.
* Samples on the SD card (`/MANTIS/SAMPLES/hat_closed.raw`, `hat_open.raw`, `kick.raw`, `snare.raw`) are used
  automatically. A pad plays, in order of preference: a sample you recorded on it, then the SD card sample, then
  the built-in sound. The pad shows SMP or SD.
* To clear a pad's sample: long-press the pad to arm it, then tap it again. It goes back to the built-in sound and
  stays that way (even after a restart) until you record a new sample on it. The file on the SD card is never
  deleted.
* The visual metronome: the lane under the header shows every 16th of the bar (the one in red) with a playhead,
  and a frame round the screen flashes on every beat and ticks on the 8ths and 16ths.
