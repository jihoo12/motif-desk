#include <JuceHeader.h>
#include "Engine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace motif {
namespace {
const auto bg = juce::Colour(0xff16141d), panel = juce::Colour(0xff211e2a),
           line = juce::Colour(0xff393345), text = juce::Colour(0xfff5ebf9),
           dim = juce::Colour(0xffaba1b7), accent = juce::Colour(0xfff6a5bc),
           mint = juce::Colour(0xffa5ddc9);
constexpr double totalBeats = 16.0, step = 0.25;
constexpr int pianoWidth = 50, rulerHeight = 24, rows = 25;
double snap(double beat) { return std::round(beat / step) * step; }
bool collides(const Project& p, const Note& key, std::optional<size_t> except = {}) {
    for (size_t i = 0; i < p.notes.size(); ++i) {
        const auto& n = p.notes[i];
        if (except && *except == i) continue;
        if (n.pitch == key.pitch && n.beat < key.beat + key.length - 1e-8
                && key.beat < n.beat + n.length - 1e-8)
            return true;
    }
    return false;
}
Project starter() {
    Project p;
    p.notes = {
        {64,0,.75,.78f},{67,1,.50,.87f},{69,1.75,.75,.8f},{67,2.75,1,.75f},
        {62,4,.75,.75f},{64,5,.5,.8f},{67,5.75,1.5,.84f},
        {64,8,.75,.76f},{67,9,.50,.87f},{72,9.75,.75,.85f},{71,10.75,1,.76f},
        {67,12,.50,.76f},{64,12.75,.50,.73f},{62,13.5,.5,.72f},{60,14.25,1.5,.82f}
    };
    return p;
}
} // namespace

class Roll final : public juce::Component {
public:
    Roll(Project& p, Engine& e) : project(p), engine(e) {
        setWantsKeyboardFocus(true);
        setTitle("Piano roll");
        setDescription("Four bars. Click to add, drag to move, drag the right edge to resize, right click to erase.");
    }
    std::function<void()> beforeEdit, afterEdit;
    std::function<bool(const juce::KeyPress&)> otherKey;
    void resetSelection() { chosen.reset(); drag = Mode::none; repaint(); }
    void scrollPitch(int semitones) {
        high = juce::jlimit(rows - 1, 127, high + semitones);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(panel);
        const auto w = beatWidth(), h = rowHeight();
        if (w <= 0 || h <= 0) return;
        g.setFont(11.0f);
        for (int i = 0; i < rows; ++i) {
            const auto k = high - i;
            const bool black = juce::MidiMessage::isMidiNoteBlack(k);
            const auto y = rulerHeight + h * static_cast<float>(i);
            g.setColour(black ? juce::Colour(0xff221e2e) : juce::Colour(0xff292537));
            g.fillRect(static_cast<float>(pianoWidth), y, static_cast<float>(getWidth() - pianoWidth), h);
            g.setColour(black ? juce::Colour(0xff302936) : juce::Colour(0xff4e4152));
            g.fillRect(1.0f, y + 1, static_cast<float>(pianoWidth - 3), juce::jmax(1.0f, h - 1));
            if (k % 12 == 0 || k % 12 == 7) {
                g.setColour(k % 12 == 0 ? text : dim);
                g.drawText(juce::MidiMessage::getMidiNoteName(k, true, true, 3), 6, juce::roundToInt(y), pianoWidth - 10,
                           juce::roundToInt(h), juce::Justification::centredLeft);
            }
            g.setColour(line);
            g.fillRect(static_cast<float>(pianoWidth), y, static_cast<float>(getWidth() - pianoWidth), k % 12 == 0 ? 1.3f : .65f);
        }
        for (int q = 0; q <= 64; ++q) {
            auto x = pianoWidth + w * static_cast<float>(q) / 4.0f;
            g.setColour(line.withMultipliedAlpha(q % 4 == 0 ? 1.0f : .45f));
            g.fillRect(x, static_cast<float>(rulerHeight), q % 16 == 0 ? 2.0f : .75f, static_cast<float>(getHeight() - rulerHeight));
        }
        g.setColour(juce::Colour(0xff3b2b44));
        g.fillRect(pianoWidth, 0, getWidth() - pianoWidth, rulerHeight);
        g.setColour(accent);
        for (int b = 0; b < 4; ++b)
            g.drawText("BAR " + juce::String(b+1), juce::roundToInt(pianoWidth + b * w * 4 + 8), 0, 48, rulerHeight, juce::Justification::centredLeft);
        for (size_t i = 0; i < project.notes.size(); ++i) {
            const auto& n = project.notes[i];
            if (n.pitch > high || n.pitch < high - rows + 1) continue;
            auto rect = noteBounds(n);
            const auto selected = chosen && *chosen == i;
            g.setColour(selected ? accent : mint.withMultipliedBrightness(.92f));
            g.fillRoundedRectangle(rect, 3.0f);
            g.setColour(juce::Colour(0xff2f213c).withAlpha(.58f));
            g.fillRoundedRectangle(rect.removeFromRight(juce::jmin(4.0f, rect.getWidth())), 1.5f);
        }
        const auto x = pianoWidth + w * static_cast<float>(engine.positionBeat());
        g.setColour(accent.withAlpha(.88f));
        g.fillRect(x, 0.0f, 1.5f, static_cast<float>(getHeight()));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        grabKeyboardFocus();
        drag = Mode::none;
        if (e.y < rulerHeight) {
            if (e.x >= pianoWidth) engine.seek(juce::jlimit(0.0, totalBeats - step, rawBeat(e.x)));
            return;
        }
        if (e.x < pianoWidth) {
            engine.audition(pitchAt(e.y));
            return;
        }
        auto hit = hitAt(e.position);
        if (e.mods.isPopupMenu()) {
            if (hit) remove(*hit);
            return;
        }
        if (!e.mods.isLeftButtonDown()) return;
        if (!hit) {
            Note n{pitchAt(e.y), juce::jlimit(0.0, totalBeats-step, std::floor(rawBeat(e.x)/step)*step), .5, .8f};
            n.length = std::min(n.length, totalBeats - n.beat);
            if (collides(project, n)) return;
            if (beforeEdit) beforeEdit();
            project.notes.push_back(n);
            hit = project.notes.size()-1;
            publish();
            recordedDrag = true;
        } else recordedDrag = false;
        chosen = hit;
        original = project.notes[*chosen];
        origin = e.position;
        drag = e.position.x >= noteBounds(original).getRight()-5.0f
            && noteBounds(original).getWidth() >= 9.0f && !recordedDrag ? Mode::resize : Mode::move;
        engine.audition(original.pitch);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag == Mode::none || !chosen || *chosen >= project.notes.size()) return;
        auto n = original;
        const auto dx = static_cast<double>(e.position.x-origin.x)/beatWidth();
        if (drag == Mode::resize) n.length = juce::jlimit(step, totalBeats-n.beat, snap(n.length+dx));
        else {
            n.beat = juce::jlimit(0.0, totalBeats-n.length, snap(n.beat+dx));
            n.pitch = juce::jlimit(0, 127, n.pitch - juce::roundToInt((e.position.y-origin.y)/rowHeight()));
        }
        auto& prev = project.notes[*chosen];
        if ((prev.pitch == n.pitch && prev.beat == n.beat && prev.length == n.length)
                || collides(project,n,chosen)) return;
        if (!recordedDrag && beforeEdit) beforeEdit();
        recordedDrag = true;
        const bool pitchChanged = prev.pitch != n.pitch;
        prev = n;
        if (pitchChanged && !engine.isPlaying()) engine.audition(n.pitch);
        publish();
    }
    void mouseUp(const juce::MouseEvent&) override { drag = Mode::none; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
        if (std::abs(wheel.deltaY) > .01f) scrollPitch(wheel.deltaY > 0 ? 2 : -2);
    }
    bool keyPressed(const juce::KeyPress& k) override {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && chosen) {
            remove(*chosen);
            return true;
        }
        return otherKey && otherKey(k);
    }
private:
    enum class Mode {none,move,resize};
    Project& project;
    Engine& engine;
    int high = 78;
    std::optional<size_t> chosen;
    Note original{};
    juce::Point<float> origin;
    Mode drag = Mode::none;
    bool recordedDrag = false;
    float beatWidth() const { return static_cast<float>(getWidth()-pianoWidth) / static_cast<float>(totalBeats); }
    float rowHeight() const { return static_cast<float>(getHeight()-rulerHeight) / static_cast<float>(rows); }
    double rawBeat(int x) const { return static_cast<double>(x-pianoWidth) / beatWidth(); }
    int pitchAt(int y) const {
        return high - juce::jlimit(0, rows-1, static_cast<int>(static_cast<float>(y-rulerHeight)/rowHeight()));
    }
    juce::Rectangle<float> noteBounds(const Note& n) const {
        return {pianoWidth + beatWidth()*static_cast<float>(n.beat) + 1, rulerHeight + rowHeight()*static_cast<float>(high-n.pitch) + 1,
                juce::jmax(2.0f, beatWidth()*static_cast<float>(n.length)-2), juce::jmax(3.0f,rowHeight()-2)};
    }
    std::optional<size_t> hitAt(juce::Point<float> pt) const {
        for (size_t i = project.notes.size(); i > 0; --i)
            if (noteBounds(project.notes[i-1]).contains(pt)) return i-1;
        return {};
    }
    void remove(size_t index) {
        if (index >= project.notes.size()) return;
        if (beforeEdit) beforeEdit();
        project.notes.erase(project.notes.begin()+static_cast<std::ptrdiff_t>(index));
        resetSelection();
        publish();
    }
    void publish() { if (afterEdit) afterEdit(); repaint(); }
};

class Desk final : public juce::Component, private juce::Timer {
public:
    Desk() : roll(project, engine) {
        setWantsKeyboardFocus(true);
        setSize(1080, 720);
        for (auto* b : {&play,&stop,&importWav,&save,&open,&midi,&clear,&down,&up,&undo}) {
            addAndMakeVisible(*b);
            b->setColour(juce::TextButton::buttonColourId, line);
            b->setColour(juce::TextButton::textColourOffId, text);
        }
        play.setColour(juce::TextButton::buttonColourId, accent);
        play.setColour(juce::TextButton::textColourOffId, bg);
        addAndMakeVisible(roll);
        addAndMakeVisible(bpm);
        bpm.setRange(45,240,1);
        bpm.setSliderStyle(juce::Slider::IncDecButtons);
        bpm.setTextBoxStyle(juce::Slider::TextBoxLeft,false,52,26);
        bpm.setColour(juce::Slider::textBoxTextColourId,text);
        bpm.setColour(juce::Slider::textBoxOutlineColourId,line);
        bpm.setTitle("Tempo");
        bpm.setValue(project.bpm,juce::dontSendNotification);
        bpm.onValueChange = [this] { remember(); project.bpm=bpm.getValue(); sync(); };
        addAndMakeVisible(gain);
        gain.setSliderStyle(juce::Slider::LinearHorizontal);
        gain.setRange(0,1,.01);
        gain.setValue(.7,juce::dontSendNotification);
        gain.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        gain.setColour(juce::Slider::trackColourId,mint);
        gain.setTitle("Backing level");
        gain.onValueChange=[this]{engine.setBackingGain(static_cast<float>(gain.getValue()));};
        play.onClick = [this]{ togglePlay(); roll.grabKeyboardFocus(); };
        stop.onClick = [this]{ engine.setPlaying(false); engine.seek(0); updateTransport(); };
        importWav.onClick = [this]{ chooseImport(); };
        save.onClick = [this]{ chooseSave(); };
        open.onClick = [this]{ chooseOpen(); };
        midi.onClick = [this]{ chooseMidi(); };
        clear.onClick = [this]{ engine.clearBacking(); backing=juce::File{}; peaks.clear(); backingSeconds=0; repaint(); };
        down.onClick = [this]{ roll.scrollPitch(-12); roll.grabKeyboardFocus(); };
        up.onClick = [this]{ roll.scrollPitch(12); roll.grabKeyboardFocus(); };
        undo.onClick = [this]{ undoEdit(); };
        roll.beforeEdit = [this]{ remember(); };
        roll.afterEdit = [this]{ sync(); };
        roll.otherKey = [this](const juce::KeyPress& k){ return handleKey(k); };
        sync();
        engine.setBackingGain(.7f);
        auto error = devices.initialise(0,2,nullptr,true);
        if (error.isNotEmpty()) audioError = "No audio output: " + error;
        devices.addAudioCallback(&engine);
        startTimerHz(30);
    }
    ~Desk() override { stopTimer(); devices.removeAudioCallback(&engine); }
    void resized() override {
        auto area=getLocalBounds().reduced(24,14);
        area.removeFromTop(58);
        auto bar=area.removeFromTop(40);
        auto place=[&](juce::Component& c,int w){c.setBounds(bar.removeFromLeft(w).reduced(3,5));};
        place(play,75); place(stop,63); place(bpm,104); place(undo,70);
        bar.removeFromLeft(12); place(open,62); place(save,65); place(midi,100); place(importWav,118);
        area.removeFromTop(12);
        auto tracks=area.removeFromTop(104);
        ruler=tracks.withTrimmedLeft(150).withTrimmedBottom(4);
        auto side=tracks.removeFromLeft(145);
        gain.setBounds(side.removeFromBottom(29).withTrimmedRight(14));
        clear.setBounds(side.removeFromBottom(26).removeFromLeft(86).reduced(2,1));
        area.removeFromTop(42);
        auto footer=area.removeFromBottom(32);
        up.setBounds(footer.removeFromRight(71).reduced(2,2));
        down.setBounds(footer.removeFromRight(71).reduced(2,2));
        roll.setBounds(area.withTrimmedLeft(100));
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(bg);
        g.setColour(text); g.setFont(juce::FontOptions(26.0f,juce::Font::bold));
        g.drawText("Motif Desk",24,16,220,32,juce::Justification::centredLeft);
        g.setFont(13.0f); g.setColour(dim);
        g.drawText("FOUR-BAR SONGWRITING SKETCHPAD",25,47,440,16,juce::Justification::centredLeft);
        g.setColour(accent);
        g.drawText("LOOP  /  4 BARS",getWidth()-168,30,144,22,juce::Justification::centredRight);
        g.setColour(panel); g.fillRoundedRectangle(ruler.toFloat(),9);
        g.setColour(text); g.setFont(juce::FontOptions(13.5f,juce::Font::bold));
        g.drawText("01  Backing",26,131,130,22,juce::Justification::centredLeft);
        g.setFont(11.0f); g.setColour(dim);
        g.drawText("WAV AUDIO  /  LEVEL",26,150,130,20,juce::Justification::centredLeft);
        g.setColour(text); g.setFont(juce::FontOptions(14.0f,juce::Font::bold));
        g.drawText("02  Melody  /  Piano",26,241,350,22,juce::Justification::centredLeft);
        g.setColour(dim);g.setFont(12.0f);
        g.drawText("Click: add    Drag: move    Right edge: resize    Right-click: erase",26,getHeight()-43,getWidth()-220,26,juce::Justification::centredLeft);
        auto inner=ruler.reduced(11,10);
        auto name=backing==juce::File{} ? "No backing loaded • Import WAV to start" : backing.getFileName();
        g.drawFittedText(name,inner.removeFromTop(20),juce::Justification::centredLeft,1);
        if (!peaks.empty()) {
            g.setColour(mint.withAlpha(.8f));
            const auto loopSeconds = totalBeats*60/project.bpm;
            const int waveWidth = juce::jlimit(0,inner.getWidth(),
                juce::roundToInt(inner.getWidth()*std::min(1.0,backingSeconds/loopSeconds)));
            for (int x=0;x<waveWidth;++x) {
                const double rel=(x/static_cast<double>(inner.getWidth()))*loopSeconds/std::max(.001,backingSeconds);
                const auto index=std::min(peaks.size()-1,static_cast<size_t>(rel*peaks.size()));
                const auto height=std::max(1.0f,peaks[index]*static_cast<float>(inner.getHeight())*.87f);
                g.fillRect(static_cast<float>(inner.getX()+x),inner.getCentreY()-height/2,1.0f,height);
            }
        }
        if (audioError.isNotEmpty() || status.isNotEmpty()) {
            g.setColour(audioError.isNotEmpty()?accent:mint);
            g.drawFittedText(audioError.isNotEmpty()?audioError:status,26,getHeight()-17,getWidth()-50,14,juce::Justification::centredLeft,1);
        }
    }
    bool keyPressed(const juce::KeyPress& k) override { return handleKey(k); }
private:
    Project project=starter();
    Engine engine;
    juce::AudioDeviceManager devices;
    Roll roll;
    juce::TextButton play{"Play"},stop{"Stop"},importWav{"Import WAV"},save{"Save"},open{"Open"},
        midi{"Export MIDI"},clear{"Clear WAV"},down{"Octave –"},up{"Octave +"},undo{"Undo"};
    juce::Slider bpm, gain;
    juce::Rectangle<int> ruler;
    juce::File backing;
    std::vector<float> peaks;
    double backingSeconds=0;
    juce::String status,audioError;
    std::unique_ptr<juce::FileChooser> dialog;
    std::vector<Project> undoState;
    bool handleKey(const juce::KeyPress& k) {
        if (k.getKeyCode()==juce::KeyPress::spaceKey && !k.getModifiers().isCommandDown()) { togglePlay(); return true; }
        if (k.getModifiers().isCommandDown()) {
            switch (juce::CharacterFunctions::toLowerCase(k.getTextCharacter())) {
                case 's': chooseSave(); return true;
                case 'o': chooseOpen(); return true;
                case 'z': undoEdit(); return true;
                default: break;
            }
        }
        return false;
    }
    void remember() {
        if (undoState.size()>=64) undoState.erase(undoState.begin());
        undoState.push_back(project);
        undo.setEnabled(true);
    }
    void undoEdit() {
        if (undoState.empty()) return;
        project=std::move(undoState.back()); undoState.pop_back(); roll.resetSelection();
        bpm.setValue(project.bpm,juce::dontSendNotification);
        sync();
    }
    void sync() { engine.setProject(project); undo.setEnabled(!undoState.empty()); repaint(); }
    void togglePlay() { engine.setPlaying(!engine.isPlaying()); updateTransport(); }
    void updateTransport() { play.setButtonText(engine.isPlaying()?"Pause":"Play"); roll.repaint(); }
    void timerCallback() override { updateTransport(); }
    void notice(const juce::String& s) { status=s; repaint(); }
    bool setBackingFile(const juce::File& f) {
        auto result=engine.loadBacking(f);
        if (result.failed()) { notice(result.getErrorMessage()); return false; }
        backing=f; peaks=engine.waveform(); backingSeconds=engine.backingDurationSeconds(); repaint(); return true;
    }
    void pick(juce::String title,juce::String filter,int flags,std::function<void(const juce::File&)> callback) {
        if (dialog) return;
        dialog=std::make_unique<juce::FileChooser>(title,juce::File{},filter);
        auto weak=juce::Component::SafePointer<Desk>(this);
        dialog->launchAsync(flags,[weak,callback=std::move(callback)](const juce::FileChooser& chooser){
            if (auto* self=weak.getComponent()) {
                auto f=chooser.getResult();
                juce::MessageManager::callAsync([weak,f,callback]{
                    if (auto* desk=weak.getComponent()) {
                        desk->dialog.reset();
                        if (f!=juce::File{}) callback(f);
                    }
                });
                juce::ignoreUnused(self);
            }
        });
    }
    void chooseImport() {
        pick("Choose a WAV backing track","*.wav",juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
             [this](const auto& f){ if(setBackingFile(f)) notice("Loaded " + f.getFileName()); });
    }
    void chooseSave() {
        pick("Save your sketch","*.motif",juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
             [this](const auto& file){
            auto f=file.hasFileExtension("motif")?file:file.withFileExtension("motif");
            juce::DynamicObject::Ptr doc=new juce::DynamicObject();
            doc->setProperty("version",1); doc->setProperty("bpm",project.bpm);
            doc->setProperty("backingGain",gain.getValue());
            doc->setProperty("backing",backing==juce::File{}?juce::String{}:backing.getRelativePathFrom(f.getParentDirectory()));
            juce::Array<juce::var> notes;
            for(const auto& n:project.notes){
                juce::DynamicObject::Ptr o=new juce::DynamicObject();
                o->setProperty("pitch",n.pitch); o->setProperty("beat",n.beat); o->setProperty("length",n.length);
                o->setProperty("velocity",static_cast<double>(n.velocity)); notes.add(juce::var(o.get()));
            }
            doc->setProperty("notes",juce::var(notes));
            notice(f.replaceWithText(juce::JSON::toString(juce::var(doc.get())))?"Saved "+f.getFileName():"Could not write the sketch.");
        });
    }
    void chooseOpen() {
        pick("Open a sketch","*.motif",juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
             [this](const auto& f){
            if (f.getSize()>1024*1024) {notice("Sketch file is too large.");return;}
            const auto doc=juce::JSON::parse(f);
            auto noteArray=doc.getProperty("notes",{}); auto* a=noteArray.getArray();
            auto tempo=static_cast<double>(doc.getProperty("bpm",0));
            if(!doc.isObject() || int(doc.getProperty("version",0))!=1 || !a || a->size()>4096
                    || !std::isfinite(tempo) || tempo<45 || tempo>240) {notice("Not a Motif Desk sketch.");return;}
            Project next; next.bpm=tempo;
            for(const auto& v:*a){
                Note n{int(v.getProperty("pitch",-1)),double(v.getProperty("beat",-1)),double(v.getProperty("length",0)),
                       float(v.getProperty("velocity",0))};
                if(!v.isObject() || n.pitch<0 || n.pitch>127 || !std::isfinite(n.beat) || !std::isfinite(n.length) ||
                   !std::isfinite(n.velocity) || n.beat<0 || n.length<step || n.beat+n.length>totalBeats + 1e-8 ||
                   n.velocity<=0 || n.velocity>1 || collides(next,n)) { notice("Sketch contains an invalid note."); return; }
                next.notes.push_back(n);
            }
            engine.setPlaying(false); engine.seek(0); engine.clearBacking();
            backing=juce::File{}; peaks.clear(); backingSeconds=0;
            project=std::move(next); undoState.clear(); roll.resetSelection();
            bpm.setValue(project.bpm,juce::dontSendNotification);
            auto volume=double(doc.getProperty("backingGain",.7));
            if(!std::isfinite(volume))volume=.7;
            gain.setValue(juce::jlimit(0.,1.,volume),juce::dontSendNotification);
            engine.setBackingGain(static_cast<float>(gain.getValue())); sync();
            auto path=doc.getProperty("backing",{}).toString();
            if (path.isNotEmpty() && !setBackingFile(f.getParentDirectory().getChildFile(path)))
                notice("Opened sketch but its backing WAV could not be found or read.");
            else notice("Opened " + f.getFileName());
        });
    }
    void chooseMidi() {
        pick("Export the piano roll","*.mid",juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const auto& file){
            auto f=file.hasFileExtension("mid")?file:file.withFileExtension("mid");
            juce::MidiMessageSequence seq;
            auto tempo=juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::lround(60000000/project.bpm)));
            seq.addEvent(tempo);
            for (const auto& n: project.notes) {
                seq.addEvent(juce::MidiMessage::noteOff(1,n.pitch).withTimeStamp((n.beat+n.length)*480.0));
            }
            for (const auto& n: project.notes) {
                seq.addEvent(juce::MidiMessage::noteOn(1,n.pitch,n.velocity).withTimeStamp(n.beat*480.0));
            }
            juce::MidiFile mf; mf.setTicksPerQuarterNote(480); mf.addTrack(seq);
            juce::MemoryOutputStream encoded;
            const bool ok=mf.writeTo(encoded,0)&&f.replaceWithData(encoded.getData(),encoded.getDataSize());
            notice(ok?"Exported " + f.getFileName():"Could not export the MIDI file.");
        });
    }
};
} // namespace motif

class MotifApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "Motif Desk";}
    const juce::String getApplicationVersion() override {return "0.1.0";}
    void initialise(const juce::String&) override { window=std::make_unique<Window>(); }
    void shutdown() override {window.reset();}
private:
    class Window final : public juce::DocumentWindow {
    public:
        Window() : juce::DocumentWindow("Motif Desk",juce::Colour(0xff16141d),allButtons,true) {
            setUsingNativeTitleBar(true);
            setResizable(true,true);
            setResizeLimits(850,590,1920,1200);
            setContentOwned(new motif::Desk(),true);
            centreWithSize(getWidth(),getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override {if(auto* app=juce::JUCEApplication::getInstance())app->systemRequestedQuit();}
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(MotifApplication)
