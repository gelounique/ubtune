#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

/** One-octave note keyboard (MetaTune style) with INPUT / OUTPUT heatmaps on top.
    In-scale keys are lit, the sung note flashes white, the target note gets a bar.
    Click a key to add / remove that note from the scale. Monochrome by design. */
class PianoKeys : public juce::Component
{
public:
    static constexpr int kHeatBins = 288;
    PianoKeys() = default;
    void setState (float inputMidi, int targetNote, uint16_t mask, int key, float level) { in = inputMidi; target = targetNote; msk = mask; root = key; lvl = level; }
    void advance (float dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { const int h = noteAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    std::function<void (int pitchClass)> onToggle;
    juce::Rectangle<int> heatRow (int i) const { return { 0, i * 20, getWidth(), 16 }; }

private:
    static bool isBlack (int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    juce::Rectangle<float> keysArea() const { return getLocalBounds().toFloat().withTrimmedTop (44.0f); }
    juce::Rectangle<float> keyRect (int pc) const;
    float xForPc (float pc) const;
    int noteAt (juce::Point<float>) const;

    float in = -1.0f, lvl = 0.0f, t = 0.0f;
    int target = -1, root = 0, hover = -1;
    uint16_t msk = 0x0FFF;
    std::array<float, 12> glow {}, tglow {};
    std::array<float, kHeatBins> heatIn {}, heatOut {};
};
