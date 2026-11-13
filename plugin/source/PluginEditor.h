#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/ParticleRing.h"
#include "ui/PianoKeys.h"

/** Rounded-rectangle button (mode segments, info). White = active. */
class RectButton : public juce::Component
{
public:
    explicit RectButton (juce::String t) : title (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setOn (bool b) { if (b != on) { on = b; repaint(); } }
    void setTitle (const juce::String& t) { if (t != title) { title = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
    float fontSize = 13.0f;
private:
    juce::String title;
    bool on = false;
};

/** "KEY  ‹ A ›" style picker: arrows step, click on the value opens a menu. */
class Selector : public juce::Component
{
public:
    Selector (juce::String label, juce::StringArray items) : lbl (std::move (label)), opts (std::move (items)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setIndex (int i, bool custom = false) { if (i != idx || custom != isCustom) { idx = i; isCustom = custom; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { const int z = zoneAt (e.x); if (z != hoverZone) { hoverZone = z; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hoverZone = -2; repaint(); }
    std::function<void (int)> onPick;
    juce::String customText { "CUSTOM" };
    float valueSize = 22.0f;
private:
    int zoneAt (int x) const { return x < 40 ? -1 : (x > getWidth() - 40 ? 1 : 0); }
    juce::String lbl;
    juce::StringArray opts;
    int idx = 0, hoverZone = -2;
    bool isCustom = false;
};

/** The big standalone DETECT KEY button with progress fill. */
class DetectButton : public juce::Component
{
public:
    DetectButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setState (float progress, const juce::String& lastResult) { if (progress != prog || lastResult != result) { prog = progress; result = lastResult; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
private:
    float prog = -1.0f;
    juce::String result;
};

/** Transpose stepper "‹ +0 st ›". */
class Stepper : public juce::Component, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    Stepper (UBTuneProcessor& p, const juce::String& paramId, juce::String label);
    ~Stepper() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override { repaint(); }
    UBTuneProcessor& proc;
    juce::RangedAudioParameter* param;
    juce::String lbl;
};

class Knob : public juce::Component
{
public:
    Knob (UBTuneProcessor& p, const juce::String& paramId, juce::String labelText, float resetValue, int size);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String label;
    int size;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class CreditsPanel : public juce::Component
{
public:
    CreditsPanel() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClose;
private:
    juce::Rectangle<int> linkSite, linkUb, linkIg;
};

class UBTuneMain : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1000, H = 700;
    explicit UBTuneMain (UBTuneProcessor&);
    ~UBTuneMain() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (float dt);   // also used by the snapshot tool
    void showCredits (bool b) { credits.setVisible (b); }

private:
    void timerCallback() override;
    void syncFromParams();

    UBTuneProcessor& proc;
    theme::LookAndFeel lnf;
    ParticleRing ring;
    PianoKeys piano;
    juce::OwnedArray<RectButton> modeBtns;
    Selector keySel, scaleSel;
    Stepper transpose;
    DetectButton detectBtn;
    RectButton infoBtn { "i" };
    std::unique_ptr<Knob> retune, humanize, formant, mix;
    juce::Slider output;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt;
    CreditsPanel credits;
    juce::Rectangle<int> logoArea, barArea, outputArea, latencyArea;
    juce::Path knobPanel;
    int lastDetectSerial = 0;
    juce::String lastDetected;
    double lastTime = 0;
};

class UBTuneEditor : public juce::AudioProcessorEditor
{
public:
    explicit UBTuneEditor (UBTuneProcessor&);
    ~UBTuneEditor() override = default;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }
    UBTuneMain& getMain() { return main; }
private:
    UBTuneMain main;
};
