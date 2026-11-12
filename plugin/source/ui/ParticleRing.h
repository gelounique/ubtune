#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

/** The animated lila particle ring in the middle of UBTune. Shows key, scale, the sung pitch
    as a comet around a 12-note wheel and reacts to level / correction / mode. */
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
        juce::String scaleName { "MINOR PENTATONIC" };
        uint16_t mask = 0x0FFF;
        int   mode = 1;
        float detectProgress = -1.0f;
    };

    ParticleRing();
    void setState (const State& s) { st = s; }
    void advance (float dt);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    std::function<void()> onClick;

private:
    struct P { float a, rOff, spd, size, phase, tw; };
    std::vector<P> ps;
    State st;
    float t = 0.0f, energy = 0.0f, voicedAmt = 0.0f, cometAngle = 0.0f, corrSm = 0.0f;
    std::array<float, 12> noteGlow {};
    std::vector<juce::Point<float>> trail;
};
