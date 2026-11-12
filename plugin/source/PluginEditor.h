#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/ParticleRing.h"
#include "ui/PianoKeys.h"

/** Rounded pill / segment button painted in the UNIQUEBABY lila style. */
class Pill : public juce::Component
{
public:
    Pill (juce::String t, juce::String sub = {}) : title (std::move (t)), subtitle (std::move (sub)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setOn (bool b) { if (b != on) { on = b; repaint(); } }
    void setAccent (bool b) { accent = b; }
    void setTitle (const juce::String& t) { title = t; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    std::function<void()> onClick;
    float fontSize = 12.0f;
    bool monoFont = true;
private:
    juce::String title, subtitle;
    bool on = false, accent = false;
};

class Knob : public juce::Component
{
public:
    Knob (UBTuneProcessor& p, const juce::String& paramId, juce::String labelText, float resetValue);
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedBottom (20)); }
    void paint (juce::Graphics&) override;
    juce::Slider slider;
private:
    juce::String label;
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
    static constexpr int W = 1000, H = 660;
    explicit UBTuneMain (UBTuneProcessor&);
    ~UBTuneMain() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (float dt);   // also used by the snapshot tool

private:
    void timerCallback() override;
    void syncFromParams();

    UBTuneProcessor& proc;
    theme::LookAndFeel lnf;
    ParticleRing ring;
    PianoKeys piano;
    juce::OwnedArray<Pill> modePills, keyPills, scalePills;
    Pill detectBtn { "DETECT KEY" }, infoBtn { "i" };
    juce::OwnedArray<Knob> knobs;
    CreditsPanel credits;
    juce::Rectangle<int> leftCard, rightCard, pianoCard, logoArea;
    bool showCredits = false;
    int lastDetectSerial = 0;
    double lastTime = 0;
};

class UBTuneEditor : public juce::AudioProcessorEditor
{
public:
    explicit UBTuneEditor (UBTuneProcessor&);
    ~UBTuneEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }
    UBTuneMain& getMain() { return main; }
private:
    UBTuneMain main;
};
