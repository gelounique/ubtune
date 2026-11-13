#include "ParticleRing.h"
#include "Theme.h"

static const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

static float angleForPitch (float midi) // 0 = C at the top, clockwise
{
    const float pc = std::fmod (std::fmod (midi, 12.0f) + 12.0f, 12.0f);
    return pc / 12.0f * kTwoPi;
}

ParticleRing::ParticleRing()
{
    dust.resize (1500);
    for (auto& d : dust) { respawn (d, true); d.life = U (rng) * d.maxLife; }
    core.resize (420);
    for (auto& c : core)
    {
        c.a = U (rng) * kTwoPi; c.r = std::sqrt (U (rng)) * 0.62f; c.va = (0.15f + U (rng) * 0.5f) * (U (rng) < 0.5f ? -1.0f : 1.0f);
        c.vr = 0; c.size = 0.6f + U (rng) * 1.6f; c.hue = U (rng); c.ph = U (rng) * kTwoPi; c.life = 1; c.maxLife = 1; c.free = false;
    }
    for (int i = 0; i < 700; ++i)
    {
        const float a = U (rng) * kTwoPi;
        const float r = 1.18f + std::pow (U (rng), 0.6f) * 0.42f;
        halo.push_back ({ a, r });
        haloPh.push_back (U (rng) * kTwoPi);
    }
    setInterceptsMouseClicks (false, false);
}

void ParticleRing::resized()
{
    const int w = juce::jmax (1, (int) std::ceil (getWidth() * renderScale));
    const int h = juce::jmax (1, (int) std::ceil (getHeight() * renderScale));
    if (! trail.isValid() || trail.getWidth() != w || trail.getHeight() != h)
        trail = juce::Image (juce::Image::ARGB, w, h, true);
}

void ParticleRing::respawn (Dust& d, bool onRing)
{
    d.a = U (rng) * kTwoPi;
    d.r = onRing ? 1.0f + (U (rng) - 0.5f) * 0.10f : 1.0f;
    d.va = (0.04f + U (rng) * 0.14f) * (U (rng) < 0.15f ? -1.0f : 1.0f);
    d.vr = 0.0f;
    d.maxLife = 2.0f + U (rng) * 5.0f;
    d.life = d.maxLife;
    d.size = 0.5f + std::pow (U (rng), 3.0f) * 2.6f;
    d.hue = U (rng);
    d.ph = U (rng) * kTwoPi;
    d.free = false;
}

void ParticleRing::spawnWave (float strength, float angle)
{
    if (waves.size() > 6) waves.erase (waves.begin());
    waves.push_back ({ 0.0f, 0.9f + 0.5f * U (rng), strength, angle });
}

juce::Colour ParticleRing::colourFor (float hue, float bright) const
{
    using namespace theme;
    // lila dominates, blue + light blue as accents
    juce::Colour base;
    if (hue < 0.60f)      base = lila.interpolatedWith (lilaDeep, std::abs (std::sin (hue * 9.0f)) * 0.35f);
    else if (hue < 0.78f) base = lila.interpolatedWith (blue, (hue - 0.60f) / 0.18f);
    else if (hue < 0.90f) base = blue.interpolatedWith (sky, (hue - 0.78f) / 0.12f);
    else                  base = sky.interpolatedWith (lila, (hue - 0.90f) / 0.10f);
    return base.interpolatedWith (juce::Colours::white, juce::jmax (0.0f, bright - 0.78f) * 2.2f);
}

void ParticleRing::advance (float dt)
{
    t += dt;
    const float targetE = juce::jlimit (0.0f, 1.0f, st.level * 9.0f);
    energy += (targetE - energy) * (1.0f - std::exp (-dt * (targetE > energy ? 20.0f : 3.5f)));
    const bool voiced = st.inputMidi > 0.0f;
    voicedAmt += ((voiced ? 1.0f : 0.0f) - voicedAmt) * (1.0f - std::exp (-dt * 10.0f));
    corrSm += (std::abs (st.correction) - corrSm) * (1.0f - std::exp (-dt * 8.0f));

    // shockwaves: onset, every note change, and on loud passages
    if (voiced && ! lastVoiced) spawnWave (0.8f + energy, angleForPitch (st.inputMidi));
    if (st.targetNote >= 0 && lastTarget >= 0 && st.targetNote != lastTarget)
        spawnWave (st.mode == 2 ? 1.4f : (st.mode == 1 ? 1.0f : 0.7f), angleForPitch ((float) st.targetNote));
    waveTimer -= dt;
    if (energy > 0.55f && waveTimer <= 0.0f) { spawnWave (0.45f * energy, U (rng) * kTwoPi); waveTimer = 0.6f; }
    if (! voiced && waveTimer <= -4.0f) { spawnWave (0.25f, U (rng) * kTwoPi); waveTimer = 0.0f; }
    if (st.targetNote >= 0) lastTarget = st.targetNote;
    if (! voiced) lastTarget = -1;
    lastVoiced = voiced;
    for (auto& w : waves) w.age += dt;
    waves.erase (std::remove_if (waves.begin(), waves.end(), [] (const Wave& w) { return w.age > w.dur; }), waves.end());

    const float sparkA = voiced ? angleForPitch (st.inputMidi) : spark;
    if (voiced) spark = sparkA;

    const float speed = (st.mode == 0 ? 0.55f : st.mode == 1 ? 1.0f : 1.6f) * (0.4f + energy * 2.0f);
    const float erosion = 0.08f + energy * 0.9f + corrSm * 0.5f + (st.mode == 2 ? 0.25f : 0.0f);

    for (auto& d : dust)
    {
        d.life -= dt;
        if (d.life <= 0.0f) { respawn (d, true); continue; }
        if (! d.free && U (rng) < erosion * dt * 0.25f)
        {
            d.free = true;
            d.vr = (U (rng) < 0.7f ? 1.0f : -0.6f) * (0.05f + U (rng) * 0.25f) * (1.0f + energy);
            d.life = juce::jmin (d.life, 0.6f + U (rng) * 1.8f);
            d.maxLife = d.life;
        }
        // flow-field turbulence
        const float n1 = std::sin (d.a * 3.0f + t * 0.9f + d.ph) + std::sin (d.a * 7.0f - t * 1.7f);
        d.a += (d.va * speed + n1 * 0.02f * (0.3f + energy)) * dt;
        if (d.free)
        {
            d.r += d.vr * dt;
            d.vr += std::sin (d.a * 5.0f + t * 2.0f + d.ph) * 0.15f * dt;
        }
        else
        {
            const float target = 1.0f + 0.05f * std::sin (d.a * 4.0f + t * 0.6f + d.ph) + 0.035f * std::sin (d.a * 9.0f - t * 1.3f);
            d.r += (target - d.r) * (1.0f - std::exp (-dt * 2.0f));
        }
        for (auto& w : waves)
        {
            const float wr = 0.55f + (w.age / w.dur) * 1.25f;
            if (std::abs (d.r - wr) < 0.06f) d.r += 0.20f * w.strength * dt * (1.0f - w.age / w.dur);
        }
        if (voiced)
        {
            float da = std::abs (d.a - sparkA); da = std::min (da, kTwoPi - da);
            if (da < 0.25f && ! d.free && U (rng) < dt * (2.0f + energy * 6.0f))
            {
                d.free = true; d.vr = 0.12f + U (rng) * 0.35f; d.life = d.maxLife = 0.5f + U (rng);
            }
        }
        if (d.a > kTwoPi) d.a -= kTwoPi;
        if (d.a < 0.0f) d.a += kTwoPi;
    }
    for (auto& c : core)
    {
        c.a += c.va * dt * (0.4f + energy * 1.5f);
        c.r += std::sin (t * 0.7f + c.ph) * 0.02f * dt;
        c.r = juce::jlimit (0.05f, 0.66f, c.r);
    }
    render (dt);
}

void ParticleRing::render (float dt)
{
    if (! trail.isValid()) return;
    const float keep = std::pow (st.mode == 2 ? 0.80f : (st.mode == 0 ? 0.93f : 0.88f), dt * 60.0f);
    trail.multiplyAllAlphas (keep);

    juce::Graphics g (trail);
    const float s = renderScale;
    const float cx = trail.getWidth() * 0.5f, cy = trail.getHeight() * 0.5f;
    const float R = std::min (trail.getWidth(), trail.getHeight()) * 0.30f * (1.0f + 0.03f * energy);
    const bool robot = st.mode == 2;

    for (auto& c : core)
    {
        const float rr = R * c.r;
        const float x = cx + std::sin (c.a) * rr, y = cy - std::cos (c.a) * rr;
        const float b = 0.25f + 0.25f * std::sin (t * 1.3f + c.ph) + energy * 0.3f;
        g.setColour (colourFor (0.62f + c.hue * 0.38f, b).withAlpha (0.10f + 0.16f * b));
        const float sz = c.size * s;
        g.fillRect (x, y, sz, sz);
    }

    for (auto& d : dust)
    {
        const float lifeK = juce::jlimit (0.0f, 1.0f, d.life / d.maxLife);
        float rr = R * d.r;
        if (robot) rr = R + std::round ((rr - R) / (R * 0.025f)) * R * 0.025f;
        const float x = cx + std::sin (d.a) * rr, y = cy - std::cos (d.a) * rr;
        const float tw = 0.55f + 0.45f * std::sin (t * 2.2f + d.ph);
        const float bright = juce::jlimit (0.0f, 1.0f, 0.3f + 0.45f * tw + energy * 0.35f + (d.free ? 0.15f : 0.0f));
        const float alpha = (d.free ? 0.75f * lifeK : 0.55f + 0.4f * std::min (1.0f, lifeK * 3.0f)) * (0.5f + 0.5f * bright);
        const float sz = d.size * s * (d.free ? 0.8f : 1.0f) * (1.0f + energy * 0.4f);
        g.setColour (colourFor (d.hue, bright).withAlpha (alpha));
        if (robot || d.free) g.fillRect (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        else g.fillEllipse (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
    }

    for (auto& w : waves)
    {
        const float k = w.age / w.dur;
        const float wr = R * (0.55f + k * 1.25f);
        const float al = (1.0f - k) * (1.0f - k) * juce::jmin (1.0f, w.strength);
        const int pts = 220;
        for (int i = 0; i < pts; ++i)
        {
            const float a = (float) i / pts * kTwoPi;
            float da = std::abs (a - w.a0); da = std::min (da, kTwoPi - da);
            const float focus = 0.45f + 0.55f * std::exp (-da * da * 1.5f);
            const float jag = 1.0f + 0.05f * std::sin (a * 13.0f + w.a0 * 5.0f + t * 4.0f) + 0.03f * (U (rng) - 0.5f);
            const float x = cx + std::sin (a) * wr * jag, y = cy - std::cos (a) * wr * jag;
            g.setColour (colourFor (0.82f + 0.12f * std::sin (a * 3.0f), 0.9f).withAlpha (al * focus * 0.8f));
            const float sz = (1.2f + 1.6f * focus) * s;
            g.fillRect (x - sz * 0.5f, y - sz * 0.5f, sz, sz);
        }
    }

    if (voicedAmt > 0.05f)
    {
        const float x = cx + std::sin (spark) * R * 1.02f, y = cy - std::cos (spark) * R * 1.02f;
        const float sz = (6.0f + energy * 8.0f) * s;
        g.setColour (theme::sky.withAlpha (0.25f * voicedAmt)); g.fillEllipse (x - sz, y - sz, sz * 2, sz * 2);
        g.setColour (juce::Colours::white.withAlpha (0.95f * voicedAmt)); g.fillEllipse (x - sz * 0.3f, y - sz * 0.3f, sz * 0.6f, sz * 0.6f);
    }
}

void ParticleRing::paint (juce::Graphics& g)
{
    using namespace theme;
    auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.30f;

    {
        juce::ColourGradient disc (juce::Colours::white.withAlpha (0.0f), c.x, c.y, juce::Colours::white.withAlpha (0.0f), c.x + R * 1.62f, c.y, true);
        disc.addColour (0.62, juce::Colours::white.withAlpha (0.015f));
        disc.addColour (0.86, juce::Colours::white.withAlpha (0.05f));
        disc.addColour (0.97, juce::Colours::white.withAlpha (0.10f));
        g.setGradientFill (disc);
        g.fillEllipse (c.x - R * 1.62f, c.y - R * 1.62f, R * 3.24f, R * 3.24f);
        for (size_t i = 0; i < halo.size(); ++i)
        {
            const auto& h = halo[i];
            const float tw = 0.5f + 0.5f * std::sin (t * 1.7f + haloPh[i]);
            const float x = c.x + std::sin (h.x) * R * h.y, y = c.y - std::cos (h.x) * R * h.y;
            const float edge = 1.0f - std::abs (h.y - 1.55f) * 2.0f;
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, 0.05f + 0.22f * tw * juce::jmax (0.2f, edge))));
            g.fillRect (x, y, 1.2f, 1.2f);
        }
        juce::ColourGradient glow (lila.withAlpha (0.0f), c.x, c.y, lila.withAlpha (0.0f), c.x + R * 1.35f, c.y, true);
        glow.addColour (0.66, lila.withAlpha (0.06f + 0.10f * energy));
        glow.addColour (0.80, blue.withAlpha (0.04f + 0.05f * energy));
        g.setGradientFill (glow);
        g.fillEllipse (c.x - R * 1.35f, c.y - R * 1.35f, R * 2.7f, R * 2.7f);
    }

    if (trail.isValid())
        g.drawImage (trail, b, juce::RectanglePlacement::stretchToFit);

    if (st.detectProgress >= 0.0f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, R * 0.78f, R * 0.78f, 0, 0, kTwoPi * juce::jmax (0.01f, st.detectProgress), true);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (dim);
    g.setFont (mono (10.5f, true));
    g.drawText (st.detectProgress >= 0 ? "LISTENING ..." : "KEY", juce::Rectangle<float> (c.x - 100, c.y - R * 0.60f, 200, 16), juce::Justification::centred);
    const bool custom = st.scaleName == "CUSTOM";
    const juce::String keyTxt = custom ? juce::String ("CUSTOM") : juce::String (kNoteNames[st.key % 12]);
    g.setColour (text);
    g.setFont (sans (custom ? R * 0.28f : R * 0.66f, 700));
    g.drawText (keyTxt, juce::Rectangle<float> (c.x - R, c.y - R * 0.44f, R * 2, R * 0.72f), juce::Justification::centred);
    g.setColour (text.withAlpha (0.75f));
    g.setFont (mono (11.5f, true));
    g.drawText (st.scaleName, juce::Rectangle<float> (c.x - R, c.y + R * 0.30f, R * 2, 16), juce::Justification::centred);

    juce::String live = "--";
    if (st.inputMidi > 0)
    {
        const int nr = (int) std::lround (st.inputMidi);
        const int cents = (int) std::lround ((st.inputMidi - nr) * 100.0f);
        live = juce::String (kNoteNames[((nr % 12) + 12) % 12]) + juce::String (nr / 12 - 1) + "  " + (cents >= 0 ? "+" : "") + juce::String (cents) + "c";
        if (st.targetNote >= 0) live << "  >  " << kNoteNames[st.targetNote % 12] << juce::String (st.targetNote / 12 - 1);
    }
    g.setColour (st.inputMidi > 0 ? text.withAlpha (0.9f) : faint);
    g.setFont (mono (10.5f));
    g.drawText (live, juce::Rectangle<float> (c.x - R, c.y + R * 0.48f, R * 2, 16), juce::Justification::centred);
}
