#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    // UBTune 1.1 — black & white UI, colour lives only in the particle ring (lila + blue + light blue)
    const juce::Colour bg        { 0xff050506 };
    const juce::Colour card      { 0xff0e0e10 };
    const juce::Colour card2     { 0xff17171a };
    const juce::Colour card3     { 0xff222226 };
    const juce::Colour line      { 0x16ffffff };
    const juce::Colour text      { 0xfff4f4f6 };
    const juce::Colour dim       { 0xff8b8b92 };
    const juce::Colour faint     { 0xff47474e };

    // ring palette only
    const juce::Colour lila      { 0xffa855f7 };
    const juce::Colour lilaDeep  { 0xff6d28d9 };
    const juce::Colour blue      { 0xff3b82f6 };
    const juce::Colour sky       { 0xff7dd3fc };

    constexpr float radius = 12.0f;   // rounded rectangles, no pills

    juce::Font sans (float size, int weight = 400);   // Instrument Sans 400/600/700
    juce::Font mono (float size, bool medium = false); // JetBrains Mono
    juce::Image logo();

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                bool hasSubMenu, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour) override;
        juce::Font getPopupMenuFont() override { return mono (13.0f, true); }
        void getIdealPopupMenuItemSize (const juce::String&, bool isSeparator, int standardHeight, int& w, int& h) override;
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };
}
