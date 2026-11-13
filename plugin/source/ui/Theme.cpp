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
    setColour (juce::PopupMenu::backgroundColourId, card2);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, text);
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::TooltipWindow::backgroundColourId, card2);
    setColour (juce::TooltipWindow::textColourId, text);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) return sansTf (f.isBold() ? 700 : 400);
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (card2);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (0, 0, w, h, 1);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& t, bool sep, int, int& w, int& h)
{
    if (sep) { w = 50; h = 9; return; }
    h = 30;
    w = (int) juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), t) + 48;
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                     bool, const juce::String& t, const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator) { g.setColour (line); g.fillRect (area.reduced (8, 4).withHeight (1)); return; }
    auto r = area.reduced (4, 2);
    if (isHighlighted && isActive) { g.setColour (text); g.fillRoundedRectangle (r.toFloat(), 7.0f); }
    else if (isTicked) { g.setColour (card3); g.fillRoundedRectangle (r.toFloat(), 7.0f); }
    g.setColour (isHighlighted && isActive ? bg : (isTicked ? text : text.withAlpha (isActive ? 0.8f : 0.3f)));
    g.setFont (getPopupMenuFont());
    g.drawText (t, r.withTrimmedLeft (12), juce::Justification::centredLeft);
    if (isTicked) { g.fillEllipse ((float) r.getRight() - 16.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f); }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float cy = b.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (b.getX(), cy - 2.0f, b.getWidth(), 4.0f, 2.0f);
    const float zx = b.getX() + (float) s.valueToProportionOfLength (0.0) * b.getWidth();
    g.setColour (text);
    g.fillRoundedRectangle (juce::jmin (zx, pos), cy - 2.0f, std::abs (pos - zx), 4.0f, 2.0f);
    g.setColour (text);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
    g.setColour (bg);
    g.fillEllipse (pos - 2.5f, cy - 2.5f, 5.0f, 5.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    // MetaTune-style segmented ring, monochrome
    const int size = (int) s.getProperties().getWithDefault ("ubSize", 1); // 0 small, 1 medium, 2 big
    const juce::String label = s.getProperties().getWithDefault ("ubLabel", "").toString();
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float zero = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;

    // outer plate
    g.setColour (juce::Colour (0xff111113)); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (juce::Colours::white.withAlpha (s.isMouseOverOrDragging() ? 0.20f : 0.09f));
    g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, r * 2 - 1, r * 2 - 1, 1.0f);

    const int segs = size == 2 ? 64 : (size == 1 ? 46 : 32);
    const float band = size == 2 ? 20.0f : (size == 1 ? 14.0f : 10.0f);
    const float ro = r - 7.0f, ri = ro - band;
    const float segW = size == 2 ? 2.6f : 2.2f;
    for (int i = 0; i < segs; ++i)
    {
        const float f = (i + 0.5f) / segs;
        const float a = a0 + f * (a1 - a0);
        const bool lit = bipolar ? (f >= std::min (zero, pos) && f <= std::max (zero, pos)) : f <= pos;
        const auto p0 = c.getPointOnCircumference (ri, a), p1 = c.getPointOnCircumference (ro, a);
        if (lit)
        {
            const float k = bipolar ? 1.0f - std::abs (f - pos) * 0.8f : 0.35f + 0.65f * (f / juce::jmax (0.01f, pos));
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.25f, 1.0f, k)));
        }
        else g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawLine ({ p0, p1 }, segW);
    }
    // head marker
    {
        const float a = a0 + pos * (a1 - a0);
        const auto p0 = c.getPointOnCircumference (ri - 4.0f, a), p1 = c.getPointOnCircumference (ro + 3.0f, a);
        g.setColour (juce::Colours::white);
        g.drawLine ({ p0, p1 }, segW + 1.5f);
    }

    // face
    const float kr = ri - (size == 2 ? 10.0f : 6.0f);
    juce::ColourGradient body (juce::Colour (0xff1d1d21), c.x, c.y - kr, juce::Colour (0xff070708), c.x, c.y + kr, false);
    g.setGradientFill (body); g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.0f);

    const float vs = size == 2 ? 30.0f : (size == 1 ? 20.0f : 14.0f);
    g.setColour (text);
    g.setFont (mono (vs, true));
    g.drawFittedText (s.getTextFromValue (s.getValue()), juce::Rectangle<int> ((int) (c.x - kr), (int) (c.y - vs * 0.75f), (int) (kr * 2), (int) (vs * 1.1f)),
                      juce::Justification::centred, 1);
    if (label.isNotEmpty())
    {
        g.setColour (dim);
        g.setFont (sans (size == 2 ? 15.0f : (size == 1 ? 12.5f : 10.5f), 600));
        g.drawFittedText (label, juce::Rectangle<int> ((int) (c.x - kr), (int) (c.y + vs * 0.42f), (int) (kr * 2), 18), juce::Justification::centred, 1);
    }
}
}
