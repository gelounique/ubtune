#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** Animated piano: shows the sung pitch (glow + floating puck), the tuned target note and
    the active scale. Click a key to toggle that pitch class (custom scale). */
class PianoKeys : public juce::Component
{
public:
    PianoKeys (int lowest = 36, int highest = 84) : lo (lowest), hi (highest) {}
    void setState (float inputMidi, int targetNote, uint16_t mask, int key) { in = inputMidi; target = targetNote; msk = mask; root = key; }
    void advance (float dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = noteAt (e.position); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    std::function<void (int pitchClass)> onToggle;

private:
    static bool isBlack (int n) { const int pc = n % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    juce::Rectangle<float> keyRect (int note) const;
    float xForPitch (float midi) const;
    int noteAt (juce::Point<float>) const;
    juce::Rectangle<float> keysArea() const { return getLocalBounds().toFloat().withTrimmedTop (22.0f); }

    int lo, hi, hover = -1;
    float in = -1.0f, puckX = -1.0f, puckAlpha = 0.0f;
    int target = -1, root = 0;
    uint16_t msk = 0x0FFF;
    std::array<float, 128> glow {}, tglow {};
};
