#include "ProjectIO.h"
#include <iostream>
#include <string_view>

namespace {
const char* usage =
    "Motif Desk 0.1.0\n\n"
    "  motif-desk render <sketch.motif> <out.wav> [--bars 2:4]\n"
    "  motif-desk compare <before.motif> <after.motif> <out.wav> [--bars 2:4]\n"
    "  motif-desk export-midi <sketch.motif> <out.mid>\n\n"
    "Bars are 1-based and inclusive (1:4 by default).\n"
    "Compare renders A, 600 ms of silence, then B.\n"
    "Existing output files are not overwritten.\n";
int fail(const juce::String& message, int code = 1) {
    std::cerr << "motif-desk: " << message << '\n';
    return code;
}
bool bars(std::string_view t, int& first, int& last) {
    if (t.size() != 3 || t[0] < '1' || t[0] > '4' || t[1] != ':' || t[2] < t[0] || t[2] > '4') return false;
    first = t[0]-'0'; last = t[2]-'0';
    return true;
}
juce::File path(const char* v) {
    return juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(v));
}
}
int main(int argc, char** argv) {
    const std::string_view command = argc >= 2 ? argv[1] : "--help";
    if (command == "--help" || command == "-h") { std::cout << usage; return 0; }
    if (command == "--version" && argc == 2) { std::cout << "0.1.0\n"; return 0; }
    const bool midi = command == "export-midi", compare = command == "compare";
    if (!midi && !compare && command != "render") return fail("Unknown command; try --help.", 2);
    const int required = compare ? 5 : 4;
    if (argc != required && (midi || argc != required+2)) return fail("Wrong number of arguments; try --help.", 2);
    int first = 1, last = 4;
    if (argc > required && (std::string_view(argv[required]) != "--bars" || !bars(argv[required+1],first,last)))
        return fail("Use --bars START:END with 1 <= START <= END <= 4.", 2);
    const auto out = path(argv[required-1]);
    if (out.exists() || out.isSymbolicLink()) return fail("Output already exists: " + out.getFullPathName());
    if (!out.hasFileExtension(midi ? "mid;midi" : "wav"))
        return fail(midi ? "Output must end in .mid or .midi." : "Output must end in .wav.", 2);

    motif::Sketch a;
    auto status = motif::readSketch(path(argv[2]), a);
    if (status.failed()) return fail(status.getErrorMessage());
    if (midi) status = motif::writeMidi(out, a.project);
    else {
        motif::Sketch b;
        if (compare) {
            status = motif::readSketch(path(argv[3]), b);
            if (status.failed()) return fail(status.getErrorMessage());
            if (a.project.bpm != b.project.bpm || a.backing != b.backing || a.backingGain != b.backingGain)
                return fail("Compare needs the same BPM, WAV, and backing gain in both sketches.");
        }
        juce::AudioBuffer<float> left;
        status = motif::Engine::renderOffline(a.project,a.backing,a.backingGain,first,last,left);
        if (status.failed()) return fail(status.getErrorMessage());
        if (!compare) status = motif::writeWav(out, left);
        else {
            juce::AudioBuffer<float> right;
            status = motif::Engine::renderOffline(b.project,b.backing,b.backingGain,first,last,right);
            if (status.failed()) return fail(status.getErrorMessage());
            constexpr int gap = 28800;
            juce::AudioBuffer<float> combined(2, left.getNumSamples()+gap+right.getNumSamples());
            combined.clear();
            for (int ch=0;ch<2;++ch) {
                combined.copyFrom(ch,0,left,ch,0,left.getNumSamples());
                combined.copyFrom(ch,left.getNumSamples()+gap,right,ch,0,right.getNumSamples());
            }
            status = motif::writeWav(out, combined);
        }
    }
    if (status.failed()) return fail(status.getErrorMessage());
    std::cout << "Wrote " << out.getFullPathName() << '\n';
}
