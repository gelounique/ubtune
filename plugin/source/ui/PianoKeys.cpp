#include "PianoKeys.h"
#include "Theme.h"

static const char* kNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const int kWhiteIdx[] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

juce::Rectangle<float> PianoKeys::keyRect (int pc) const
{
    auto a = keysArea();
    const float ww = a.getWidth() / 7.0f;
    if (! isBlack (pc)) return { a.getX() + kWhiteIdx[pc] * ww, a.getY(), ww, a.getHeight() };
    const float bw = ww * 0.60f;
    return { a.getX() + kWhiteIdx[pc - 1] * ww + ww - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.60f };
}

float PianoKeys::xForPc (float pc) const
{
    pc = std::fmod (std::fmod (pc, 12.0f) + 12.0f, 12.0f);
    const int p0 = (int) std::floor (pc);
    const float f = pc - p0;
    const float x0 = keyRect (p0).getCentreX();
    const float x1 = p0 == 11 ? keysArea().getRight() + (keyRect (0).getCentreX() - keysArea().getX()) : keyRect (p0 + 1).getCentreX();
    float x = x0 + (x1 - x0) * f;
    if (x > keysArea().getRight()) x -= keysArea().getWidth();
    return x;
}

int PianoKeys::noteAt (juce::Point<float> p) const
{
    if (! keysArea().contains (p)) return -1;
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc) && keyRect (pc).contains (p)) return pc;
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc) && keyRect (pc).contains (p)) return pc;
    return -1;
}

void PianoKeys::mouseDown (const juce::MouseEvent& e)
{
    const int pc = noteAt (e.position);
    if (pc >= 0 && onToggle) onToggle (pc);
}

void PianoKeys::advance (float dt)
{
    t += dt;
    const int sung = in > 0 ? ((int) std::lround (in) % 12 + 12) % 12 : -1;
    const int tpc = target >= 0 ? target % 12 : -1;
    for (int i = 0; i < 12; ++i)
    {
        const float a = i == sung ? 1.0f : 0.0f;
        glow[(size_t) i] += (a - glow[(size_t) i]) * (1.0f - std::exp (-dt * (a > glow[(size_t) i] ? 30.0f : 4.0f)));
        const float b = i == tpc ? 1.0f : 0.0f;
        tglow[(size_t) i] += (b - tglow[(size_t) i]) * (1.0f - std::exp (-dt * (b > tglow[(size_t) i] ? 30.0f : 5.0f)));
    }
    // heatmaps: continuous energy along the octave
    const float decay = std::exp (-dt * 1.6f);
    const float w = (float) getWidth();
    const float e = juce::jlimit (0.0f, 1.0f, 0.35f + lvl * 8.0f);
    for (int b = 0; b < kHeatBins; ++b)
    {
        heatIn[(size_t) b] *= decay;
        heatOut[(size_t) b] *= decay;
    }
    if (w > 0)
    {
        auto splat = [&] (std::array<float, kHeatBins>& h, float x, float amt, float spread)
        {
            const float c = x / w * kHeatBins;
            for (int b = (int) (c - spread * 3); b <= (int) (c + spread * 3); ++b)
            {
                const int bb = (b % kHeatBins + kHeatBins) % kHeatBins;
                const float d = (b - c) / spread;
                h[(size_t) bb] = juce::jmin (1.0f, h[(size_t) bb] + amt * std::exp (-d * d) * dt * 6.0f);
            }
        };
        if (in > 0) splat (heatIn, xForPc (in), e, 2.5f);
        if (target >= 0) splat (heatOut, keyRect (target % 12).getCentreX(), e, 1.6f);
    }
}

void PianoKeys::paint (juce::Graphics& g)
{
    using namespace theme;
    // ---- heatmaps ----
    for (int row = 0; row < 2; ++row)
    {
        auto r = heatRow (row).toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 4.0f);
        const auto& h = row == 0 ? heatIn : heatOut;
        const float bw = r.getWidth() / kHeatBins;
        for (int b = 0; b < kHeatBins; ++b)
        {
            const float v = h[(size_t) b];
            if (v < 0.01f) continue;
            const float n = 0.75f + 0.25f * std::sin (b * 12.9898f + t * 7.0f);
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, v * n)));
            const float hh = r.getHeight() * (row == 0 ? juce::jlimit (0.3f, 1.0f, v * 1.3f) : 1.0f);
            g.fillRect (r.getX() + b * bw, r.getCentreY() - hh * 0.5f, bw + 0.6f, hh);
        }
    }

    // ---- keys ----
    auto drawKey = [&] (int pc)
    {
        auto r = keyRect (pc);
        const bool black = isBlack (pc);
        const bool inScale = (msk >> pc) & 1;
        auto kr = black ? r : r.reduced (1.5f, 0.0f);
        const float rad = 6.0f;
        juce::Colour base = black ? (inScale ? juce::Colour (0xff3a3a41) : juce::Colour (0xff0c0c0e))
                                  : (inScale ? juce::Colour (0xff5c5c65) : juce::Colour (0xff19191c));
        if (pc == hover) base = base.brighter (0.25f);
        juce::ColourGradient gr (base.brighter (0.10f), kr.getX(), kr.getY(), base.darker (0.25f), kr.getX(), kr.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (kr, rad);

        const float gl = glow[(size_t) pc], tg = tglow[(size_t) pc];
        if (gl > 0.01f)
        {
            juce::ColourGradient lg (juce::Colours::white.withAlpha (0.95f * gl), kr.getCentreX(), kr.getBottom(), juce::Colours::white.withAlpha (0.25f * gl), kr.getCentreX(), kr.getY(), false);
            g.setGradientFill (lg);
            g.fillRoundedRectangle (kr, rad);
        }
        if (tg > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (tg));
            g.fillRoundedRectangle (kr.getX() + 4.0f, kr.getY() + 34.0f, kr.getWidth() - 8.0f, 3.0f, 1.5f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (kr, rad, 1.0f);

        // label at the top of the key
        g.setColour (gl > 0.5f ? bg : (inScale ? text : dim));
        g.setFont (sans (pc == root ? 17.0f : 15.0f, 700));
        g.drawText (kNames[pc], kr.withHeight (30.0f), juce::Justification::centred);
        if (pc == root && inScale)
        {
            g.setColour (gl > 0.5f ? bg : text);
            g.fillEllipse (kr.getCentreX() - 2.5f, kr.getY() + 25.0f, 5.0f, 5.0f);
        }
    };
    for (int pc = 0; pc < 12; ++pc) if (! isBlack (pc)) drawKey (pc);
    for (int pc = 0; pc < 12; ++pc) if (isBlack (pc)) drawKey (pc);
}
