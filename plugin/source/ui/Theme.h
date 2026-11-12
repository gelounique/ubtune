#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    // UNIQUEBABY dark look — lila edition
    const juce::Colour bg        { 0xff07050b };
    const juce::Colour bg2       { 0xff0d0a13 };
    const juce::Colour card      { 0xff14111b };
    const juce::Colour card2     { 0xff1b1724 };
    const juce::Colour line      { 0x14ffffff };
    const juce::Colour text      { 0xfff5f1fb };
    const juce::Colour dim       { 0xff8e879b };
    const juce::Colour faint     { 0xff4a4456 };
    const juce::Colour lila      { 0xffa855f7 };
    const juce::Colour lilaLight { 0xffd2a8ff };
    const juce::Colour lilaDeep  { 0xff6d28d9 };
    const juce::Colour magenta   { 0xffe879f9 };

    juce::Font sans (float size, int weight = 400);   // Instrument Sans 400/600/700
    juce::Font mono (float size, bool medium = false); // JetBrains Mono
    juce::Image logo();

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>&) override;
        juce::Font getSliderPopupFont (juce::Slider&) override { return mono (12.0f); }
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };
}
