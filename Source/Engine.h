#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace motif
{
struct Note
{
    int pitch = 60;
    double beat = 0.0;
    double length = 1.0;
    float velocity = 0.8f;
};

struct Project
{
    double bpm = 120.0;
    std::vector<Note> notes;
};

// All editing methods are called by one owner (the message thread).
// Attach a single callback and detach it before destroying the engine.
class Engine final : public juce::AudioIODeviceCallback
{
public:
    static constexpr double loopBeats = 16.0;

    Engine();
    ~Engine() override = default;

    void setProject (const Project&);
    juce::Result loadBacking (const juce::File&);
    void clearBacking();
    void setPlaying (bool);
    bool isPlaying() const;
    void seek (double beat);
    double positionBeat() const;
    void audition (int midiPitch);
    void setBackingGain (float linear);
    std::vector<float> waveform() const;
    double backingDurationSeconds() const;

    // Renders an inclusive, one-based four-bar subsection at 48 kHz stereo.
    // An empty File omits the backing. Leave output untouched on failure.
    static juce::Result renderOffline (const Project&, const juce::File& backing, float backingGain,
                                       int firstBar, int lastBar, juce::AudioBuffer<float>& output);

    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;

private:
    struct Backing;
    struct Snapshot
    {
        Project project;
        std::shared_ptr<const Backing> backing;
        float backingGain = 0.8f;
        unsigned seekId = 0, auditionId = 0, playbackId = 0;
        double seekBeat = 0.0;
        int auditionPitch = 60;
        bool playing = false;
    };

    struct Voice
    {
        double phase = 0.0, phaseStep = 0.0;
        double age = 0.0, heldSeconds = 0.0;
        float velocity = 0.0f, pan = 0.0f;
        bool active = false, preview = false;
    };

    void publish();
    void startVoice (int pitch, float velocity, double seconds, bool preview);
    void silenceArrangement();
    void resetNoteCursor (const Project&);

    Snapshot editing;
    std::array<Snapshot, 3> buffers;
    unsigned back = 2, front = 0;
    std::atomic<unsigned> middle { 1 };

    std::atomic<bool> playing { false };
    std::atomic<double> reportedBeat { 0.0 };
    // See seek(): a newer GUI seek is visible even when no device runs.
    std::atomic<unsigned> appliedSeek { 0 };

    std::array<Voice, 32> voices;
    double currentBeat = 0.0, rate = 44100.0;
    std::size_t noteCursor = 0;
    unsigned lastSeek = 0, lastAudition = 0, lastPlayback = 0;
    bool audioPlaying = false;
};
}
