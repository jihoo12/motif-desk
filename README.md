# Motif Desk

A small JUCE desktop sketchpad for the first four bars of a melody. Put a WAV
underneath the piano roll, edit a few notes while the loop plays, then export
the melody as standard MIDI to keep arranging or bring into OpenUtau.

## Start on NixOS / Linux

```sh
nix run
```

An X11 display or an XWayland session is required for this Linux GUI. If
your Wayland compositor does not provide XWayland, launch it from an X11
or XWayland-enabled desktop session. The app uses JUCE's ALSA audio backend.

```sh
nix build
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
nix develop -c cmake --build build
nix develop -c ctest --test-dir build --output-on-failure
```

## Sketch

- Play or press **Space** to loop four bars. **Stop** returns to bar one;
  click the bar ruler to seek.
- Click an empty square to add a note, drag to move it, or drag its right
  edge to change the duration. Right-click to erase, or select and press
  **Delete**. The grid snaps to sixteenth notes. Scroll or use the octave
  buttons to change the pitch range; click a piano key to audition it.
- **Import WAV** accepts mono or stereo WAV. It starts at bar one and plays
  at its original speed; changing BPM changes the length of the four-bar
  loop, not the speed of the backing. Use the track slider for its level.
- **Save** writes a `.motif` sketch with the tempo, notes, backing level and
  a path to the WAV; it does not copy audio into the project. Keep the
  WAV alongside the project if you want to move them together.
- **Export MIDI** exports the piano roll and tempo, not the backing audio.
  OpenUtau can import the MIDI; assign a singer and write the lyrics there.

This is a first version: one piano roll, one backing WAV, fixed 4/4 and
four bars. No microphone recording, plug-in hosting or automatic time stretch.
The starter melody is an original sketch and there are no bundled voicebanks
or songs.

## License

The project source is Apache-2.0. JUCE is fetched separately and has its own
[license terms](https://juce.com/legal/juce-8-licence/); check those before
redistributing a linked build.
