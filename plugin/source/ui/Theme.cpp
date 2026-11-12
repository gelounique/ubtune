#include "Theme.h"
#include "BinaryData.h"

namespace theme
{
static juce::Typeface::Ptr tf (const void* d, size_t n) { return juce::Typeface::createSystemTypefaceFor (d, n); }

static juce::Typeface::Ptr sansTf (int w)
{
    static auto r = tf (BinaryData::InstrumentSansRegular_ttf, BinaryData::InstrumentSansRegular_ttfSize);
    static auto s = tf (BinaryData::InstrumentSansSemiBold_ttf, BinaryData::InstrumentSansSemiBold_ttfSize);
    static auto b = tf (BinaryData::InstrumentSansBold_ttf, BinaryData::InstrumentSansBold_ttfSize);
    return w >= 700 ? b : (w >= 600 ? s : r);
}
static juce::Typeface::Ptr monoTf (bool m)
{
    static auto r = tf (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    static auto md = tf (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize);
    return m ? md : r;
}

juce::Font sans (float size, int weight) { return juce::Font (juce::FontOptions (sansTf (weight)).withHeight (size)); }
juce::Font mono (float size, bool medium) { return juce::Font (juce::FontOptions (monoTf (medium)).withHeight (size)); }

juce::Image logo()
{
    static juce::Image img = juce::ImageCache::getFromMemory (BinaryData::ublogo_png, BinaryData::ublogo_pngSize);
    return img;
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::BubbleComponent::backgroundColourId, card2);
    setColour (juce::TooltipWindow::backgroundColourId, card2);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, card);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return sansTf (f.isBold() ? 700 : 400);
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (11.0f));
    l->setColour (juce::Label::textColourId, dim);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centred);
    return l;
}

void LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour (card2); g.fillRoundedRectangle (body, 8.0f);
    g.setColour (lila.withAlpha (0.5f)); g.drawRoundedRectangle (body.reduced (0.5f), 8.0f, 1.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zeroAng = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    const float trackR = r - 3.0f;

    // track
    juce::Path track; track.addCentredArc (c.x, c.y, trackR, trackR, 0, a0, a1, true);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc with glow
    juce::Path arc; arc.addCentredArc (c.x, c.y, trackR, trackR, 0, std::min (zeroAng, ang), std::max (zeroAng, ang), true);
    const bool en = s.isEnabled();
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (lila.withAlpha (en ? 0.07f : 0.02f));
        g.strokePath (arc, juce::PathStrokeType (4.0f + i * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::ColourGradient grad (lilaDeep, c.x - r, c.y + r, magenta, c.x + r, c.y - r, false);
    g.setGradientFill (grad);
    g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // knob body
    const float kr = trackR - 9.0f;
    juce::ColourGradient body (juce::Colour (0xff26212f), c.x, c.y - kr, juce::Colour (0xff100d15), c.x, c.y + kr, false);
    g.setGradientFill (body); g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::white.withAlpha (s.isMouseOverOrDragging() ? 0.16f : 0.08f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);

    // pointer dot
    const auto p = c.getPointOnCircumference (kr - 7.0f, ang);
    g.setColour (lila.withAlpha (0.35f)); g.fillEllipse (p.x - 5, p.y - 5, 10, 10);
    g.setColour (lilaLight); g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5, 5);

    // value text
    g.setColour (text);
    g.setFont (mono (std::max (10.0f, kr * 0.36f), true));
    g.drawFittedText (s.getTextFromValue (s.getValue()), juce::Rectangle<int> ((int) (c.x - kr), (int) (c.y - 9), (int) (kr * 2), 18), juce::Justification::centred, 1);
}
}
