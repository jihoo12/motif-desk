#pragma once
#include "Engine.h"
#include <optional>

namespace motif {
struct Sketch {
    Project project;
    juce::File backing;
    float backingGain = .7f;
};
constexpr double minimumNoteLength = .25;
bool collides(const Project&, const Note&, std::optional<std::size_t> except = {});
juce::Result readSketch(const juce::File&, Sketch&);
juce::Result writeSketch(const juce::File&, const Sketch&);
juce::Result writeMidi(const juce::File&, const Project&);
juce::Result writeWav(const juce::File&, const juce::AudioBuffer<float>&);
}
