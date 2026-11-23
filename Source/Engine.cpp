#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace motif
{
namespace
{
constexpr unsigned dirty = 4;
constexpr int waveformPoints = 512;
constexpr std::size_t maxNotes = 4096;
constexpr double maxBackingSeconds = 600.0;
constexpr juce::int64 maxBackingFrames = 16'000'000;
constexpr double twoPi = 6.2831853071795864769;

double safeBeat (double beat)
{
    if (!std::isfinite (beat))
        return 0.0;

    return std::clamp (beat, 0.0, std::nextafter (Engine::loopBeats, 0.0));
}

float clip (float sample)
{
    if (sample > 0.95f)
        return 0.95f + 0.05f * std::tanh ((sample - 0.95f) * 20.0f);
    if (sample < -0.95f)
        return -0.95f + 0.05f * std::tanh ((sample + 0.95f) * 20.0f);
    return sample;
}
}

struct Engine::Backing
{
    juce::AudioBuffer<float> audio;
    double rate = 0.0;
    std::vector<float> peaks;
};

Engine::Engine() = default;

void Engine::publish()
{
    // The producer may only mutate back, the consumer may only read front.
    // The atomic exchange transfers the otherwise unowned third slot. A
    // replaced backing is released by the producer, never in the callback.
    buffers[back] = editing;
    back = middle.exchange (back | dirty, std::memory_order_acq_rel) & ~dirty;
}

void Engine::setProject (const Project& project)
{
    editing.project.bpm = std::isfinite (project.bpm) ? std::clamp (project.bpm, 40.0, 240.0) : 120.0;
    editing.project.notes.clear();
    editing.project.notes.reserve (std::min (project.notes.size(), maxNotes));

    for (const auto& note : project.notes)
    {
        if (editing.project.notes.size() == maxNotes)
            break;
        if (note.pitch < 0 || note.pitch > 127 || !std::isfinite (note.beat)
            || !std::isfinite (note.length) || !std::isfinite (note.velocity)
            || note.beat < 0.0 || note.beat >= loopBeats || note.length <= 0.0 || note.velocity <= 0.0f)
            continue;

        auto valid = note;
        valid.length = std::min (valid.length, loopBeats);
        valid.velocity = std::min (valid.velocity, 1.0f);
        editing.project.notes.push_back (valid);
    }

    std::stable_sort (editing.project.notes.begin(), editing.project.notes.end(),
                      [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    publish();
}

juce::Result Engine::loadBacking (const juce::File& file)
{
    if (!file.existsAsFile() || !file.hasFileExtension ("wav;wave"))
        return juce::Result::fail ("Choose an existing WAV file.");

    auto stream = file.createInputStream();
    if (!stream || !stream->openedOk())
        return juce::Result::fail ("Could not open the WAV file.");

    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (stream.release(), true));
    if (!reader || reader->numChannels == 0 || reader->numChannels > 2 || !std::isfinite (reader->sampleRate)
        || reader->sampleRate < 1000.0 || reader->sampleRate > 384000.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail ("Could not read this mono or stereo WAV file.");

    if (reader->lengthInSamples > maxBackingFrames
        || static_cast<double> (reader->lengthInSamples) / reader->sampleRate > maxBackingSeconds)
        return juce::Result::fail ("This WAV is too long to load safely into memory.");

    auto backing = std::make_shared<Backing>();
    backing->rate = reader->sampleRate;
    const auto frames = static_cast<int> (reader->lengthInSamples);
    backing->audio.setSize (static_cast<int> (reader->numChannels), frames);
    if (!reader->read (&backing->audio, 0, frames, 0, true, reader->numChannels == 2))
        return juce::Result::fail ("The WAV audio could not be decoded.");

    // Sanitize floating-point PCM as part of decoding, not on the audio thread.
    for (int c = 0; c < backing->audio.getNumChannels(); ++c)
    {
        auto* samples = backing->audio.getWritePointer (c);
        for (int i = 0; i < frames; ++i)
            samples[i] = std::isfinite (samples[i]) ? std::clamp (samples[i], -4.0f, 4.0f) : 0.0f;
    }

    backing->peaks.resize (waveformPoints);
    for (int bin = 0; bin < waveformPoints; ++bin)
    {
        const auto begin = static_cast<int> (static_cast<juce::int64> (bin) * frames / waveformPoints);
        const auto end = juce::jlimit (0, frames, std::max (begin + 1,
            static_cast<int> (static_cast<juce::int64> (bin + 1) * frames / waveformPoints)));
        float peak = 0.0f;
        for (int c = 0; c < backing->audio.getNumChannels(); ++c)
            peak = std::max (peak, backing->audio.getMagnitude (c, begin, end - begin));
        backing->peaks[static_cast<std::size_t> (bin)] = std::min (peak, 1.0f);
    }

    editing.backing = std::move (backing);
    publish();
    return juce::Result::ok();
}

void Engine::clearBacking()
{
    editing.backing.reset();
    publish();
}

std::vector<float> Engine::waveform() const
{
    return editing.backing ? editing.backing->peaks : std::vector<float> {};
}

double Engine::backingDurationSeconds() const
{
    return editing.backing ? editing.backing->audio.getNumSamples() / editing.backing->rate : 0.0;
}

void Engine::setBackingGain (float linear)
{
    editing.backingGain = std::isfinite (linear) ? std::clamp (linear, 0.0f, 2.0f) : 0.0f;
    publish();
}

void Engine::setPlaying (bool value)
{
    if (editing.playing == value)
        return;
    editing.playing = value;
    ++editing.playbackId;
    playing.store (value, std::memory_order_release);
    publish();
}

bool Engine::isPlaying() const
{
    return playing.load (std::memory_order_acquire);
}

void Engine::seek (double beat)
{
    editing.seekBeat = safeBeat (beat);
    ++editing.seekId;
    publish();
}

double Engine::positionBeat() const
{
    if (editing.seekId != appliedSeek.load (std::memory_order_acquire))
        return editing.seekBeat;
    return reportedBeat.load (std::memory_order_acquire);
}

void Engine::audition (int midiPitch)
{
    if (midiPitch < 0 || midiPitch > 127)
        return;
    editing.auditionPitch = midiPitch;
    ++editing.auditionId;
    publish();
}

juce::Result Engine::renderOffline (const Project& project, const juce::File& backing,
                                    float backingGain, int firstBar, int lastBar,
                                    juce::AudioBuffer<float>& output)
{
    if (firstBar < 1 || lastBar > 4 || firstBar > lastBar)
        return juce::Result::fail ("Choose a bar range from 1 through 4.");

    constexpr int sampleRate = 48000;
    constexpr int blockFrames = 512;
    Engine engine;
    engine.rate = sampleRate;
    engine.setProject (project);
    engine.setBackingGain (backingGain);
    if (backing.getFullPathName().isNotEmpty())
    {
        auto loaded = engine.loadBacking (backing);
        if (loaded.failed())
            return loaded;
    }

    const auto framesAtBarBoundary = [&] (int bars)
    {
        return static_cast<int> (std::llround (bars * 4.0 * sampleRate * 60.0 / engine.editing.project.bpm));
    };
    const int start = framesAtBarBoundary (firstBar - 1);
    const int end = framesAtBarBoundary (lastBar);
    juce::AudioBuffer<float> selected (2, end - start);
    std::array<float, blockFrames> scratchL {}, scratchR {};
    float* scratch[] { scratchL.data(), scratchR.data() };
    engine.setPlaying (true);

    // Run the same callback and voices from the true beat zero. Discard the
    // preceding audio, not the preceding note events or sample history.
    for (int frame = 0; frame < end;)
    {
        const int count = std::min (blockFrames, end - frame);
        engine.audioDeviceIOCallbackWithContext (nullptr, 0, scratch, 2, count, {});
        const int copyStart = std::max (frame, start);
        if (copyStart < frame + count)
            for (int channel = 0; channel < 2; ++channel)
                selected.copyFrom (channel, copyStart - start, scratch[channel] + copyStart - frame, frame + count - copyStart);
        frame += count;
    }

    output = std::move (selected);
    return juce::Result::ok();
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double newRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    rate = std::isfinite (newRate) && newRate >= 1000.0 ? newRate : 44100.0;
    voices = {};
    resetNoteCursor (buffers[front].project);
}

void Engine::audioDeviceStopped()
{
    voices = {};
}

void Engine::resetNoteCursor (const Project& project)
{
    noteCursor = static_cast<std::size_t> (std::lower_bound (project.notes.begin(), project.notes.end(), currentBeat,
        [] (const Note& note, double beat) { return note.beat < beat; }) - project.notes.begin());
}

void Engine::silenceArrangement()
{
    for (auto& voice : voices)
        if (!voice.preview)
            voice.active = false;
}

void Engine::startVoice (int pitch, float velocity, double seconds, bool preview)
{
    auto* voice = &voices.front();
    for (auto& candidate : voices)
    {
        if (!candidate.active)
        {
            voice = &candidate;
            break;
        }
        if (candidate.age > voice->age)
            voice = &candidate;
    }
    const auto frequency = 440.0 * std::exp2 ((pitch - 69) / 12.0);
    *voice = { 0.0, twoPi * frequency, 0.0, seconds, velocity,
               std::clamp ((pitch - 64) * 0.012f, -0.4f, 0.4f), true, preview };
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* output, int channels,
                                               int samples, const juce::AudioIODeviceCallbackContext&)
{
    const bool changed = (middle.load (std::memory_order_acquire) & dirty) != 0;
    if (changed)
        front = middle.exchange (front, std::memory_order_acq_rel) & ~dirty;

    const auto& state = buffers[front];
    const auto& project = state.project;
    const bool seeks = state.seekId != lastSeek;
    if (seeks)
    {
        lastSeek = state.seekId;
        currentBeat = state.seekBeat;
        silenceArrangement();
    }

    if (state.playbackId != lastPlayback)
    {
        lastPlayback = state.playbackId;
        if (!state.playing || !audioPlaying)
            silenceArrangement();
        audioPlaying = state.playing;
    }

    if (changed)
        resetNoteCursor (project);

    if (state.auditionId != lastAudition)
    {
        lastAudition = state.auditionId;
        startVoice (state.auditionPitch, 0.75f, 0.4, true);
    }

    const double dt = 1.0 / rate;
    const double secondsPerBeat = 60.0 / project.bpm;
    const double beatStep = dt / secondsPerBeat;
    const auto* backing = state.backing.get();
    const auto frameLimit = backing != nullptr ? backing->audio.getNumSamples() : 0;
    const float* backingL = backing != nullptr ? backing->audio.getReadPointer (0) : nullptr;
    const float* backingR = backing != nullptr && backing->audio.getNumChannels() > 1
                          ? backing->audio.getReadPointer (1) : backingL;

    for (int i = 0; i < samples; ++i)
    {
        float left = 0.0f, right = 0.0f;

        if (audioPlaying)
        {
            const auto nextBeat = currentBeat + beatStep;
            while (noteCursor < project.notes.size() && project.notes[noteCursor].beat < nextBeat)
            {
                const auto& note = project.notes[noteCursor++];
                startVoice (note.pitch, note.velocity, note.length * secondsPerBeat, false);
            }

            // Resample at the absolute playhead; seek and loop stay aligned.
            // WAV remains at its original speed when project tempo changes.
            if (backingL != nullptr)
            {
                const auto at = currentBeat * secondsPerBeat * backing->rate;
                if (at < frameLimit)
                {
                    const auto base = static_cast<int> (at);
                    const auto next = std::min (base + 1, frameLimit - 1);
                    const auto frac = static_cast<float> (at - base);
                    const auto remaining = std::min (frameLimit / backing->rate - currentBeat * secondsPerBeat,
                                                     (loopBeats - currentBeat) * secondsPerBeat);
                    const auto fade = std::clamp (std::min (remaining, currentBeat * secondsPerBeat) * 250.0, 0.0, 1.0);
                    const auto gain = static_cast<float> (fade) * state.backingGain;
                    left = gain * (backingL[base] + frac * (backingL[next] - backingL[base]));
                    right = gain * (backingR[base] + frac * (backingR[next] - backingR[base]));
                }
            }

            currentBeat = nextBeat;
            if (currentBeat >= loopBeats)
            {
                currentBeat -= loopBeats;
                noteCursor = 0;
            }
        }

        for (auto& voice : voices)
        {
            if (!voice.active)
                continue;

            const auto releaseAge = std::max (0.0, voice.age - voice.heldSeconds);
            if (releaseAge > 0.28 || voice.age > 4.0)
            {
                voice.active = false;
                continue;
            }

            const auto envelope = std::min (1.0, voice.age / 0.005) * std::exp (-voice.age * 1.35 - releaseAge * 21.0);
            const auto fundamental = std::sin (voice.phase);
            const auto second = voice.phaseStep * 2 < rate * 3.14159 ? 0.24 * std::sin (voice.phase * 2) : 0.0;
            const auto third = voice.phaseStep * 3 < rate * 3.14159 ? 0.1 * std::sin (voice.phase * 3) : 0.0;
            const auto strike = voice.phaseStep * 5 < rate * 3.14159
                                  ? 0.11 * std::exp (-voice.age * 32.0) * std::sin (voice.phase * 5) : 0.0;
            const auto sound = static_cast<float> ((fundamental + second + third + strike) * envelope * voice.velocity * 0.15);
            left += sound * (1.0f - voice.pan);
            right += sound * (1.0f + voice.pan);

            voice.phase += voice.phaseStep * dt;
            if (voice.phase >= twoPi)
                voice.phase -= twoPi * std::floor (voice.phase / twoPi);
            voice.age += dt;
        }

        for (int c = 0; c < channels; ++c)
            if (output[c] != nullptr)
                output[c][i] = clip (channels == 1 ? (left + right) * 0.5f : (c % 2 == 0 ? left : right));
    }

    reportedBeat.store (currentBeat, std::memory_order_release);
    if (seeks)
        appliedSeek.store (lastSeek, std::memory_order_release);
}
}
