#include "../Source/Engine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Buffer {
    static constexpr int frames=512;
    float left[frames]{},right[frames]{};
    float* ptrs[2]{left,right};
    void tick(motif::Engine& engine) {
        engine.audioDeviceIOCallbackWithContext(nullptr,0,ptrs,2,frames,{});
    }
    float peak() const {
        float p=0;
        for(auto s:left) { check(std::isfinite(s), "Audio sample was not finite"); p=std::max(p,std::abs(s)); }
        for(auto s:right) { check(std::isfinite(s), "Audio sample was not finite"); p=std::max(p,std::abs(s)); }
        return p;
    }
};
void run() {
    motif::Engine engine;
    engine.audioDeviceAboutToStart(nullptr);
    Buffer b;
    b.tick(engine);
    check(b.peak()==0, "Stopped empty project was not silent");

    motif::Project p;
    p.notes={{64,0,.5,.8f}};
    engine.setProject(p);
    engine.setPlaying(true);
    b.tick(engine);
    check(b.peak()>.005f,"First note did not sound");
    check(engine.positionBeat()>0 && engine.positionBeat()<.1,"120 BPM transport did not advance");
    engine.seek(15.99);
    b.tick(engine);
    check(engine.positionBeat()<.1, "Transport didn't wrap across end of fourth bar");

    engine.setPlaying(false);
    b.tick(engine);
    const auto held=engine.positionBeat();
    b.tick(engine);
    check(b.peak()==0 && engine.positionBeat()==held,"Pause did not hold");
    engine.audition(69);
    b.tick(engine);
    check(b.peak()>.005f,"Stopped audition was silent");

    // Build a tiny 22.05 kHz PCM WAV fixture, no physical sound card required.
    auto f=juce::File::createTempFile(".wav");
    juce::MemoryOutputStream bytes;
    bytes.write("RIFF",4);bytes.writeInt(36+4410);
    bytes.write("WAVEfmt ",8);bytes.writeInt(16);bytes.writeShort(1);bytes.writeShort(1);
    bytes.writeInt(22050);bytes.writeInt(44100);bytes.writeShort(2);bytes.writeShort(16);
    bytes.write("data",4);bytes.writeInt(4410);
    for(int i=0;i<2205;++i)bytes.writeShort(static_cast<short>(9000*std::sin(i*.11)));
    check(f.replaceWithData(bytes.getData(),bytes.getDataSize()),"Could not create WAV fixture");
    const auto status=engine.loadBacking(f);
    f.deleteFile();
    check(status.wasOk(),"Valid WAV was rejected");
    check(!engine.waveform().empty(),"No waveform generated for imported audio");
    check(std::abs(engine.backingDurationSeconds()-.1)<.001,"Incorrect WAV duration");
    engine.audioDeviceStopped();
    engine.audioDeviceAboutToStart(nullptr);
    engine.setProject({});
    engine.seek(.07);
    engine.setPlaying(true);
    b.tick(engine);
    check(b.peak()>.04f,"Decoded/resampled backing didn't sound");
    engine.clearBacking();
    b.tick(engine);
    check(b.peak()==0,"Cleared backing continued to sound");
}
}

int main() {
    try { run(); }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "Motif Desk engine checks passed\n";
}
