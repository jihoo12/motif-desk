# Motif Desk

A small four-bar songwriting sketchpad built with JUCE. The main workflow is
headless: edit a human-readable score, render audio, and compare two takes
without opening a window or connecting an audio device.

## Render on NixOS / Linux

```sh
nix run . -- render examples/first-sketch.motif preview.wav
nix run . -- render examples/first-sketch.motif bars-2-3.wav --bars 2:3
nix run . -- export-midi examples/first-sketch.motif melody.mid
```

A `.motif` file is JSON. Start from [the included sketch](examples/first-sketch.motif):
`beat` counts quarter notes from zero, `length` is in quarter notes (minimum
0.25), `pitch` is a MIDI note number, and `velocity` is greater than zero
and at most 1. Four bars in 4/4 give you 16 beats. Leave `backing` empty or
use a path to a mono/stereo WAV, relative to the sketch, and set
`backingGain` between 0 and 1. The backing starts at bar one, at its original
speed; BPM does not time-stretch it. Files are not overwritten by the CLI.

For a quick A/B, copy the file, change just the notes, and run:

```sh
nix run . -- compare before.motif after.motif who-sings-it-better.wav --bars 2:3
```

You hear the before take, 600 ms of silence, then the after take. Keep both
sketches at the same tempo, with the same backing and level. Bar ranges are
1-based and inclusive. Range renders preserve notes ringing in from earlier
bars and the backing's original position. WAV is stereo, 48 kHz/24-bit.
MIDI export includes the melody and tempo, not the backing; OpenUtau can
import it. Motif Desk does not synthesize singing or bundle voicebanks.

## Optional desktop piano roll

```sh
nix run .#gui
```

The Linux GUI needs an X11 or XWayland-enabled session and uses JUCE's ALSA
audio backend. It reads and writes the same `.motif` files. Use Space to
play/pause, click the bar ruler to seek, click to add notes, drag to move
them or resize at the right edge, and right-click or press Delete to erase.
Scroll or use the octave buttons to change the visible pitch area.

## Build and test

```sh
nix build
nix flake check
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
nix develop -c cmake --build build
nix develop -c ctest --test-dir build --output-on-failure
```

Use `nix develop .#gui` and add `-DMOTIF_DESK_BUILD_GUI=ON` for a local GUI
build. Without Nix, point CMake at a JUCE 8.0.12 source checkout with
`-DJUCE_SOURCE_DIR=/path/to/JUCE`.

## License

The project source is Apache-2.0. The separately fetched JUCE dependency
has its own [license terms](https://juce.com/legal/juce-8-licence/); check those
before redistributing a linked build.
