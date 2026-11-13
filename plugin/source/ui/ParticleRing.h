#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

/** The UBTune orb: an abstract, decaying particle ring (lila with blue / light-blue accents)
    that erodes, leaves trails and fires shockwaves on note changes and onsets.
    Rendered into an offscreen trail buffer, crisp text on top. */
class ParticleRing : public juce::Component
{
public:
    struct State
    {
        float level = 0.0f;          // input rms
        float inputMidi = -1.0f;     // sung pitch, -1 = silence
        int   targetNote = -1;
        float correction = 0.0f;     // semitones
        int   key = 9;
        juce::String scaleName { "PENTATONIC MINOR" };
        uint16_t mask = 0x0FFF;
        int   mode = 1;
        float detectProgress = -1.0f;
    };

    ParticleRing();
    void setState (const State& s) { st = s; }
    void advance (float dt);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setRenderScale (float s) { renderScale = s; resized(); }

private:
    struct Dust { float a, r, va, vr, life, maxLife, size, hue, ph; bool free; };
    struct Wave { float age, dur, strength, a0; };

    void spawnWave (float strength, float angle);
    void respawn (Dust&, bool onRing);
    juce::Colour colourFor (float hue, float bright) const;
    void render (float dt);

    std::vector<Dust> dust, core;
    std::vector<Wave> waves;
    std::vector<juce::Point<float>> halo;   // static fuzzy outer disc
    std::vector<float> haloPh;
    juce::Image trail;
    float renderScale = 1.5f;
    State st;
    std::mt19937 rng { 11 };
    std::uniform_real_distribution<float> U { 0.0f, 1.0f };
    float t = 0.0f, energy = 0.0f, voicedAmt = 0.0f, spark = 0.0f, corrSm = 0.0f, waveTimer = 0.0f;
    int lastTarget = -1;
    bool lastVoiced = false;
};
