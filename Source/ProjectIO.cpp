#include "ProjectIO.h"
#include <cmath>

namespace motif {
namespace {
constexpr int noteLimit = 4096;
constexpr double edge = 1e-8;
bool number(const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }
}
bool collides(const Project& p, const Note& n, std::optional<std::size_t> except) {
    for (std::size_t i = 0; i < p.notes.size(); ++i) {
        if (except && i == *except) continue;
        const auto& other = p.notes[i];
        if (other.pitch == n.pitch && other.beat < n.beat + n.length - edge
                && n.beat < other.beat + other.length - edge) return true;
    }
    return false;
}
juce::Result readSketch(const juce::File& file, Sketch& out) {
    if (!file.existsAsFile()) return juce::Result::fail("Sketch does not exist: " + file.getFullPathName());
    if (file.getSize() > 1024*1024) return juce::Result::fail("Sketch file is too large.");
    juce::var doc;
    auto result = juce::JSON::parse(file.loadFileAsString(), doc);
    if (result.failed()) return result;
    auto version = doc.getProperty("version", {}), bpm = doc.getProperty("bpm", {});
    auto notes = doc.getProperty("notes", {}), backing = doc.getProperty("backing", juce::String{});
    auto level = doc.getProperty("backingGain", .7);
    const auto* array = notes.getArray();
    if (!doc.isObject() || !version.isInt() || int(version) != 1 || !array || array->size() > noteLimit
         || !number(bpm) || !std::isfinite(double(bpm)) || double(bpm) < 45 || double(bpm) > 240
         || !backing.isString() || !number(level) || !std::isfinite(double(level))
         || double(level) < 0 || double(level) > 1)
        return juce::Result::fail("Invalid Motif Desk sketch (expected version 1, BPM 45-240, backing gain 0-1).");
    Sketch next;
    next.project.bpm = double(bpm);
    next.backingGain = float(level);
    if (backing.toString().isNotEmpty())
        next.backing = file.getParentDirectory().getChildFile(backing.toString());
    for (int i = 0; i < array->size(); ++i) {
        const auto& v = array->getReference(i);
        auto x = v.getProperty("pitch", {}), b = v.getProperty("beat", {});
        auto d = v.getProperty("length", {}), g = v.getProperty("velocity", {});
        Note n{int(x), double(b), double(d), float(g)};
        if (!v.isObject() || !x.isInt() || n.pitch < 0 || n.pitch > 127 ||
             !number(b) || !number(d) || !number(g) || !std::isfinite(n.beat) ||
             !std::isfinite(n.length) || !std::isfinite(n.velocity) || n.beat < 0 ||
             n.length < minimumNoteLength || n.beat+n.length > Engine::loopBeats+edge ||
             n.velocity <= 0 || n.velocity > 1 || collides(next.project, n))
            return juce::Result::fail("Invalid or overlapping note " + juce::String(i+1) + ".");
        next.project.notes.push_back(n);
    }
    out = std::move(next);
    return juce::Result::ok();
}
juce::Result writeSketch(const juce::File& f, const Sketch& s) {
    juce::DynamicObject::Ptr doc = new juce::DynamicObject();
    doc->setProperty("version", 1); doc->setProperty("bpm", s.project.bpm);
    doc->setProperty("backingGain", double(s.backingGain));
    doc->setProperty("backing", s.backing == juce::File{} ? juce::String{} : s.backing.getRelativePathFrom(f.getParentDirectory()));
    juce::Array<juce::var> notes;
    for (const auto& n : s.project.notes) {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty("pitch", n.pitch); o->setProperty("beat", n.beat);
        o->setProperty("length", n.length); o->setProperty("velocity", double(n.velocity));
        notes.add(juce::var(o.get()));
    }
    doc->setProperty("notes", juce::var(notes));
    return f.replaceWithText(juce::JSON::toString(juce::var(doc.get())))
        ? juce::Result::ok() : juce::Result::fail("Could not write the sketch.");
}
juce::Result writeMidi(const juce::File& f, const Project& p) {
    constexpr int ppq = 480;
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60000000/p.bpm))));
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4));
    for (const auto& n : p.notes)
        seq.addEvent(juce::MidiMessage::noteOff(1,n.pitch).withTimeStamp(std::round((n.beat+n.length)*ppq)));
    for (const auto& n : p.notes)
        seq.addEvent(juce::MidiMessage::noteOn(1,n.pitch,n.velocity).withTimeStamp(std::round(n.beat*ppq)));
    seq.addEvent(juce::MidiMessage::endOfTrack().withTimeStamp(Engine::loopBeats*ppq));
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq);
    juce::MemoryOutputStream encoded;
    return mf.writeTo(encoded,0) && f.replaceWithData(encoded.getData(),encoded.getDataSize())
        ? juce::Result::ok() : juce::Result::fail("Could not write MIDI output.");
}
juce::Result writeWav(const juce::File& f, const juce::AudioBuffer<float>& audio) {
    juce::TemporaryFile tmp(f);
    auto fileStream = tmp.getFile().createOutputStream();
    if (!fileStream || !fileStream->openedOk()) return juce::Result::fail("Could not open WAV output.");
    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    juce::WavAudioFormat format;
    const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(48000)
        .withNumChannels(audio.getNumChannels()).withBitsPerSample(24);
    auto writer = format.createWriterFor(stream,opts);
    if (!writer || !writer->writeFromAudioSampleBuffer(audio,0,audio.getNumSamples()) || !writer->flush())
        return juce::Result::fail("Could not encode WAV output.");
    writer.reset();
    return tmp.overwriteTargetFileWithTemporary() ? juce::Result::ok() : juce::Result::fail("Could not save WAV output.");
}
}
