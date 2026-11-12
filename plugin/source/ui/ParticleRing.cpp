#include "ParticleRing.h"
#include "Theme.h"

static const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

ParticleRing::ParticleRing()
{
    std::mt19937 rng (7);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::uniform_real_distribution<float> ud (0.0f, 1.0f);
    ps.resize (620);
    for (auto& p : ps)
    {
        p.a = ud (rng) * juce::MathConstants<float>::twoPi;
        p.rOff = nd (rng) * 0.045f;
        p.spd = (0.05f + ud (rng) * 0.16f) * (ud (rng) < 0.12f ? -1.0f : 1.0f);
        p.size = 0.6f + std::pow (ud (rng), 3.0f) * 2.4f;
        p.phase = ud (rng) * 6.283f;
        p.tw = 0.5f + ud (rng) * 2.5f;
    }
    setInterceptsMouseClicks (true, false);
}

static float angleForPitch (float midi) // 0 = C at the top, clockwise
{
    const float pc = std::fmod (std::fmod (midi, 12.0f) + 12.0f, 12.0f);
    return pc / 12.0f * juce::MathConstants<float>::twoPi;
}

void ParticleRing::advance (float dt)
{
    t += dt;
    const float targetE = juce::jlimit (0.0f, 1.0f, st.level * 9.0f);
    energy += (targetE - energy) * (1.0f - std::exp (-dt * (targetE > energy ? 18.0f : 4.0f)));
    const bool voiced = st.inputMidi > 0.0f;
    voicedAmt += ((voiced ? 1.0f : 0.0f) - voicedAmt) * (1.0f - std::exp (-dt * 10.0f));
    corrSm += (std::abs (st.correction) - corrSm) * (1.0f - std::exp (-dt * 8.0f));

    if (voiced)
    {
        const float target = angleForPitch (st.inputMidi);
        float d = target - cometAngle;
        while (d > juce::MathConstants<float>::pi) d -= juce::MathConstants<float>::twoPi;
        while (d < -juce::MathConstants<float>::pi) d += juce::MathConstants<float>::twoPi;
        cometAngle += d * (1.0f - std::exp (-dt * (st.mode == 2 ? 40.0f : 14.0f)));
    }
    for (int i = 0; i < 12; ++i)
    {
        const bool on = st.targetNote >= 0 && (st.targetNote % 12) == i;
        noteGlow[(size_t) i] += ((on ? 1.0f : 0.0f) - noteGlow[(size_t) i]) * (1.0f - std::exp (-dt * (on ? 25.0f : 5.0f)));
    }

    const float speedMul = (st.mode == 0 ? 0.6f : st.mode == 1 ? 1.0f : 1.5f) * (0.35f + energy * 2.2f);
    for (auto& p : ps)
    {
        p.a += p.spd * dt * speedMul;
        if (p.a > juce::MathConstants<float>::twoPi) p.a -= juce::MathConstants<float>::twoPi;
        if (p.a < 0) p.a += juce::MathConstants<float>::twoPi;
    }
}

void ParticleRing::paint (juce::Graphics& g)
{
    using namespace theme;
    auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.34f;
    const float twoPi = juce::MathConstants<float>::twoPi;

    // ---- ambient glow ----
    {
        juce::ColourGradient glow (lila.withAlpha (0.20f + 0.18f * energy), c.x, c.y, lila.withAlpha (0.0f), c.x + R * 1.55f, c.y, true);
        glow.addColour (0.55, lilaDeep.withAlpha (0.10f + 0.10f * energy));
        g.setGradientFill (glow);
        g.fillEllipse (c.x - R * 1.6f, c.y - R * 1.6f, R * 3.2f, R * 3.2f);
        juce::ColourGradient inner (bg.withAlpha (0.92f), c.x, c.y, bg.withAlpha (0.0f), c.x + R * 0.95f, c.y, true);
        g.setGradientFill (inner);
        g.fillEllipse (c.x - R, c.y - R, R * 2, R * 2);
    }

    // soft halo strokes
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (lila.withAlpha (0.045f + 0.03f * energy));
        g.drawEllipse (c.x - R, c.y - R, R * 2, R * 2, 6.0f + i * 7.0f);
    }

    // ---- particles ----
    const bool robot = st.mode == 2;
    for (size_t i = 0; i < ps.size(); ++i)
    {
        const auto& p = ps[i];
        float wob = std::sin (p.a * 5.0f + t * 1.3f + p.phase) * 0.022f * (0.4f + energy)
                  + std::sin (p.a * 3.0f - t * 0.7f) * 0.014f;
        float da = std::abs (p.a - cometAngle);
        da = std::min (da, twoPi - da);
        const float near = std::exp (-(da * da) / 0.09f) * voicedAmt;
        float rr = R * (1.0f + p.rOff * (1.0f + energy * 1.6f + corrSm * 0.6f) + wob + near * 0.07f * (0.4f + energy));
        if (robot) rr = R + std::round ((rr - R) / (R * 0.02f)) * (R * 0.02f); // stepped, robotic
        const float x = c.x + std::sin (p.a) * rr, y = c.y - std::cos (p.a) * rr;
        const float tw = 0.55f + 0.45f * std::sin (t * p.tw + p.phase);
        const float bright = juce::jlimit (0.0f, 1.0f, 0.25f + 0.45f * tw + near * 0.8f + energy * 0.25f);
        const float sz = p.size * (1.0f + near * 0.9f + energy * 0.35f);
        auto col = lilaDeep.interpolatedWith (lila, juce::jmin (1.0f, bright * 1.4f))
                           .interpolatedWith (bright > 0.8f ? juce::Colours::white : magenta, juce::jmax (0.0f, bright - 0.65f) * 1.6f);
        if (sz > 2.2f)
        {
            g.setColour (col.withAlpha (0.12f * bright));
            g.fillEllipse (x - sz * 2.2f, y - sz * 2.2f, sz * 4.4f, sz * 4.4f);
        }
        g.setColour (col.withAlpha (0.35f + 0.65f * bright));
        if (robot) g.fillRect (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        else       g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
    }

    // ---- 12-note wheel ----
    const float Rn = R * 1.27f;
    for (int i = 0; i < 12; ++i)
    {
        const float a = i / 12.0f * twoPi;
        const bool inScale = (st.mask >> i) & 1;
        const bool isRoot = i == st.key;
        const auto pt = juce::Point<float> (c.x + std::sin (a) * Rn, c.y - std::cos (a) * Rn);
        const float gl = noteGlow[(size_t) i];
        if (gl > 0.02f)
        {
            g.setColour (lila.withAlpha (0.35f * gl));
            g.fillEllipse (pt.x - 17, pt.y - 17, 34, 34);
        }
        g.setColour (gl > 0.3f ? juce::Colours::white : inScale ? (isRoot ? lilaLight : text.withAlpha (0.85f)) : faint);
        g.setFont (mono (11.5f, inScale || gl > 0.3f));
        g.drawText (kNoteNames[i], juce::Rectangle<float> (pt.x - 16, pt.y - 8, 32, 16), juce::Justification::centred);
        // tick
        const auto t0 = juce::Point<float> (c.x + std::sin (a) * (R * 1.13f), c.y - std::cos (a) * (R * 1.13f));
        g.setColour (inScale ? lila.withAlpha (0.7f) : faint.withAlpha (0.5f));
        g.fillEllipse (t0.x - (inScale ? 2.0f : 1.2f), t0.y - (inScale ? 2.0f : 1.2f), inScale ? 4.0f : 2.4f, inScale ? 4.0f : 2.4f);
    }

    // ---- comet (sung pitch) ----
    if (voicedAmt > 0.02f)
    {
        const float rc = R * 1.13f;
        for (int k = 14; k >= 0; --k)
        {
            const float a = cometAngle - k * 0.022f;
            const float x = c.x + std::sin (a) * rc, y = c.y - std::cos (a) * rc;
            const float al = voicedAmt * (1.0f - k / 15.0f);
            const float s = 7.0f * (1.0f - k / 18.0f);
            g.setColour (magenta.withAlpha (0.18f * al)); g.fillEllipse (x - s * 1.8f, y - s * 1.8f, s * 3.6f, s * 3.6f);
            g.setColour (juce::Colours::white.withAlpha (0.85f * al)); g.fillEllipse (x - s * 0.4f, y - s * 0.4f, s * 0.8f, s * 0.8f);
        }
    }

    // ---- key detection progress ----
    if (st.detectProgress >= 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, R * 0.80f, R * 0.80f, 0, 0, twoPi * juce::jmax (0.01f, st.detectProgress), true);
        g.setColour (magenta.withAlpha (0.9f));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- centre text ----
    g.setColour (dim);
    g.setFont (mono (11.0f, true));
    g.drawText (st.detectProgress >= 0 ? "LISTENING ..." : "KEY", juce::Rectangle<float> (c.x - 100, c.y - R * 0.58f, 200, 16), juce::Justification::centred);

    const juce::String keyTxt = st.scaleName == "CUSTOM" ? juce::String ("CUSTOM") : juce::String (kNoteNames[st.key % 12]);
    g.setColour (text);
    g.setFont (sans (keyTxt.length() > 2 ? R * 0.30f : R * 0.62f, 700));
    g.drawText (keyTxt, juce::Rectangle<float> (c.x - R, c.y - R * 0.40f, R * 2, R * 0.66f), juce::Justification::centred);

    g.setColour (lilaLight);
    g.setFont (mono (12.0f, true));
    g.drawText (st.scaleName, juce::Rectangle<float> (c.x - R, c.y + R * 0.28f, R * 2, 18), juce::Justification::centred);

    // live note readout
    juce::String live = "--";
    if (st.inputMidi > 0)
    {
        const int nr = (int) std::lround (st.inputMidi);
        const int cents = (int) std::lround ((st.inputMidi - nr) * 100.0f);
        live = juce::String (kNoteNames[((nr % 12) + 12) % 12]) + juce::String (nr / 12 - 1) + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + "c";
        if (st.targetNote >= 0) live << "  >  " << kNoteNames[st.targetNote % 12] << juce::String (st.targetNote / 12 - 1);
    }
    g.setColour (st.inputMidi > 0 ? text.withAlpha (0.9f) : faint);
    g.setFont (mono (11.0f));
    g.drawText (live, juce::Rectangle<float> (c.x - R, c.y + R * 0.46f, R * 2, 16), juce::Justification::centred);
}
