#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/UBTuneEngine.h"

namespace ids
{
    static const juce::String mode { "mode" }, key { "key" }, scale { "scale" }, retune { "retune" },
        humanize { "humanize" }, formant { "formant" }, transpose { "transpose" }, mix { "mix" }, output { "output" };
}

class UBTuneProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kCustomScale = ubt::NumScales; // last entry of the scale choice

    UBTuneProcessor();
    ~UBTuneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "UBTune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- helpers for the UI ----
    juce::AudioProcessorValueTreeState apvts;
    ubt::Engine engine;

    int getKey() const;
    int getScale() const;
    uint16_t getActiveMask() const;
    uint16_t getCustomMask() const { return (uint16_t) customMask.load(); }
    void toggleNoteInMask (int pitchClass);
    void setParam (const juce::String& id, float plainValue);
    void applyModePreset (int mode);   // SMOOTH / PRO / T-PAIN defaults
    void applyDetectedKey (int root, bool minor);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    std::atomic<int> customMask { 0x0FFF };
    std::atomic<float>* pMode {}, *pKey {}, *pScale {}, *pRetune {}, *pHumanize {}, *pFormant {},
                      *pTranspose {}, *pMix {}, *pOutput {};
    int preparedChannels = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UBTuneProcessor)
};
