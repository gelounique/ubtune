#include "PianoKeys.h"
#include "Theme.h"

static int whiteIndex (int n) { static const int w[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 }; return (n / 12) * 7 + w[n % 12]; }

juce::Rectangle<float> PianoKeys::keyRect (int n) const
{
    auto a = keysArea();
    const int nWhite = whiteIndex (hi) - whiteIndex (lo) + 1;
    const float ww = a.getWidth() / (float) nWhite;
    const float x = a.getX() + (whiteIndex (n) - whiteIndex (lo)) * ww;
    if (! isBlack (n)) return { x, a.getY(), ww, a.getHeight() };
    const float bw = ww * 0.62f;
    return { x + ww - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.62f };
}

float PianoKeys::xForPitch (float m) const
{
    const int n0 = (int) std::floor (m);
    const float f = m - n0;
    const auto r0 = keyRect (juce::jlimit (lo, hi, n0)), r1 = keyRect (juce::jlimit (lo, hi, n0 + 1));
    return r0.getCentreX() + (r1.getCentreX() - r0.getCentreX()) * f;
}

int PianoKeys::noteAt (juce::Point<float> p) const
{
    if (! keysArea().contains (p)) return -1;
    for (int n = lo; n <= hi; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
    for (int n = lo; n <= hi; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
    return -1;
}

void PianoKeys::mouseDown (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n >= 0 && onToggle) onToggle (n % 12);
}

void PianoKeys::advance (float dt)
{
    const int sung = in > 0 ? (int) std::lround (in) : -1;
    for (int n = 0; n < 128; ++n)
    {
        const float tg = (n == sung) ? 1.0f : 0.0f;
        glow[(size_t) n] += (tg - glow[(size_t) n]) * (1.0f - std::exp (-dt * (tg > glow[(size_t) n] ? 30.0f : 5.0f)));
        const float tt = (n == target) ? 1.0f : 0.0f;
        tglow[(size_t) n] += (tt - tglow[(size_t) n]) * (1.0f - std::exp (-dt * (tt > tglow[(size_t) n] ? 30.0f : 6.0f)));
    }
    if (in > 0)
    {
        const float x = xForPitch (juce::jlimit ((float) lo, (float) hi, in));
        puckX = puckX < 0 ? x : puckX + (x - puckX) * (1.0f - std::exp (-dt * 22.0f));
    }
    puckAlpha += ((in > 0 ? 1.0f : 0.0f) - puckAlpha) * (1.0f - std::exp (-dt * 10.0f));
}

void PianoKeys::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto a = keysArea();

    // pitch rail
    const float railY = 9.0f;
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (a.getX(), railY - 1.0f, a.getWidth(), 2.0f, 1.0f);
    if (puckAlpha > 0.02f && puckX >= 0)
    {
        if (target >= lo && target <= hi)
        {
            const float tx = keyRect (target).getCentreX();
            g.setColour (magenta.withAlpha (0.55f * puckAlpha));
            g.drawLine (juce::jmin (tx, puckX), railY, juce::jmax (tx, puckX), railY, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.8f * puckAlpha));
            g.fillRect (tx - 1.0f, railY - 5.0f, 2.0f, 10.0f);
        }
        g.setColour (lila.withAlpha (0.35f * puckAlpha)); g.fillEllipse (puckX - 9, railY - 9, 18, 18);
        g.setColour (lilaLight.withAlpha (puckAlpha)); g.fillEllipse (puckX - 4, railY - 4, 8, 8);
    }

    auto drawKey = [&] (int n)
    {
        auto r = keyRect (n);
        const bool black = isBlack (n);
        const bool inScale = (msk >> (n % 12)) & 1;
        const float gl = glow[(size_t) n], tg = tglow[(size_t) n];
        auto kr = black ? r : r.reduced (1.0f, 0.0f);
        const float rad = black ? 3.0f : 5.0f;

        juce::Colour base = black ? juce::Colour (0xff09070d) : (inScale ? juce::Colour (0xff221d2c) : juce::Colour (0xff15121b));
        if (n == hover) base = base.brighter (0.25f);
        juce::ColourGradient gr (base.brighter (0.08f), kr.getX(), kr.getY(), base.darker (0.25f), kr.getX(), kr.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (kr, rad);

        if (tg > 0.01f)
        {
            g.setColour (magenta.withAlpha (0.25f * tg));
            g.fillRoundedRectangle (kr, rad);
        }
        if (gl > 0.01f)
        {
            juce::ColourGradient lg (lilaLight.withAlpha (0.95f * gl), kr.getCentreX(), kr.getBottom(), lila.withAlpha (0.15f * gl), kr.getCentreX(), kr.getY(), false);
            g.setGradientFill (lg);
            g.fillRoundedRectangle (kr, rad);
            g.setColour (lila.withAlpha (0.25f * gl));
            g.drawRoundedRectangle (kr.expanded (2.0f), rad + 2.0f, 3.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (black ? 0.10f : 0.06f));
        g.drawRoundedRectangle (kr, rad, 1.0f);

        if (inScale)
        {
            const float d = black ? 5.0f : 6.0f;
            const bool isRoot = (n % 12) == root;
            g.setColour (gl > 0.4f ? juce::Colours::white : (isRoot ? magenta : lila));
            g.fillEllipse (kr.getCentreX() - d * 0.5f, kr.getBottom() - (black ? 10.0f : 26.0f), d, d);
        }
        if (! black && n % 12 == 0)
        {
            g.setColour (gl > 0.4f ? bg : dim);
            g.setFont (mono (9.5f));
            g.drawText ("C" + juce::String (n / 12 - 1), kr.withTrimmedTop (kr.getHeight() - 16.0f), juce::Justification::centred);
        }
    };
    for (int n = lo; n <= hi; ++n) if (! isBlack (n)) drawKey (n);
    for (int n = lo; n <= hi; ++n) if (isBlack (n)) drawKey (n);
}
