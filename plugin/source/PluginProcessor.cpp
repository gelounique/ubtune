#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;

juce::AudioProcessorValueTreeState::ParameterLayout UBTuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    juce::StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::StringArray scales;
    for (int i = 0; i < ubt::NumScales; ++i) scales.add (juce::String (ubt::scaleName (i)).toLowerCase().replace ("pentatonic", "pent.").toUpperCase());
    scales.add ("CUSTOM");

    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::mode, 1 }, "Mode", juce::StringArray { "Smooth", "Pro", "T-Pain" }, 1));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::key, 1 }, "Key", notes, 9));
    p.push_back (std::make_unique<APC> (juce::ParameterID { ids::scale, 1 }, "Scale", scales, (int) ubt::PentMinor));

    auto ms = juce::NormalisableRange<float> (0.0f, 400.0f, 0.1f); ms.setSkewForCentre (40.0f);
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::retune, 1 }, "Retune Speed", ms, 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return v < 0.5f ? juce::String ("0 ms") : juce::String (v, v < 10 ? 1 : 0) + " ms"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::humanize, 1 }, "Humanize", juce::NormalisableRange<float> (0, 100, 0.1f), 25.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String ((int) std::round (v)) + " %"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::formant, 1 }, "Formant", juce::NormalisableRange<float> (-6, 6, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::transpose, 1 }, "Transpose", juce::NormalisableRange<float> (-12, 12, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String ((int) std::round (v)) + " st"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::mix, 1 }, "Mix", juce::NormalisableRange<float> (0, 100, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String ((int) std::round (v)) + " %"; })));
    p.push_back (std::make_unique<APF> (juce::ParameterID { ids::output, 1 }, "Output", juce::NormalisableRange<float> (-24, 12, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return { p.begin(), p.end() };
}

UBTuneProcessor::UBTuneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UBTUNE", createLayout())
{
    pMode = apvts.getRawParameterValue (ids::mode);
    pKey = apvts.getRawParameterValue (ids::key);
    pScale = apvts.getRawParameterValue (ids::scale);
    pRetune = apvts.getRawParameterValue (ids::retune);
    pHumanize = apvts.getRawParameterValue (ids::humanize);
    pFormant = apvts.getRawParameterValue (ids::formant);
    pTranspose = apvts.getRawParameterValue (ids::transpose);
    pMix = apvts.getRawParameterValue (ids::mix);
    pOutput = apvts.getRawParameterValue (ids::output);
    customMask = ubt::scaleMask (9, ubt::PentMinor);
    engine.prepare (48000.0, 2);
}

bool UBTuneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void UBTuneProcessor::prepareToPlay (double sampleRate, int)
{
    preparedChannels = std::max (getTotalNumInputChannels(), getTotalNumOutputChannels());
    engine.prepare (sampleRate, std::max (1, preparedChannels));
    setLatencySamples (engine.getLatency());
}

int UBTuneProcessor::getKey() const   { return juce::jlimit (0, 11, (int) pKey->load()); }
int UBTuneProcessor::getScale() const { return juce::jlimit (0, (int) kCustomScale, (int) pScale->load()); }

uint16_t UBTuneProcessor::getActiveMask() const
{
    const int s = getScale();
    return s == kCustomScale ? (uint16_t) customMask.load() : ubt::scaleMask (getKey(), s);
}

void UBTuneProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void UBTuneProcessor::toggleNoteInMask (int pc)
{
    uint16_t m = getActiveMask();
    m ^= (uint16_t) (1u << (pc % 12));
    if (m == 0) return; // never allow an empty scale
    customMask = m;
    setParam (ids::scale, (float) kCustomScale);
}

void UBTuneProcessor::applyModePreset (int mode)
{
    setParam (ids::mode, (float) mode);
    switch (mode)
    {
        case 0: setParam (ids::retune, 70.0f); setParam (ids::humanize, 60.0f); break;   // Waves Real-Time style
        case 1: setParam (ids::retune, 20.0f); setParam (ids::humanize, 25.0f); break;   // Auto-Tune Pro style
        default:                                                                          // T-Pain / MetaTune robot
        {
            setParam (ids::retune, 0.0f); setParam (ids::humanize, 0.0f);
            const int s = getScale();
            const bool minorish = (s == ubt::Minor || s == ubt::PentMinor || s == ubt::HarmonicMinor || s == ubt::Dorian || s == ubt::Blues || s == ubt::Chromatic);
            if (s != kCustomScale) setParam (ids::scale, (float) (minorish ? ubt::PentMinor : ubt::PentMajor));
        }
    }
}

void UBTuneProcessor::applyDetectedKey (int root, bool minor)
{
    const int s = getScale();
    setParam (ids::key, (float) root);
    if (s == ubt::PentMajor || s == ubt::PentMinor) setParam (ids::scale, (float) (minor ? ubt::PentMinor : ubt::PentMajor));
    else setParam (ids::scale, (float) (minor ? ubt::Minor : ubt::Major));
}

void UBTuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, buffer.getNumSamples()); // mono -> stereo

    ubt::Params p;
    p.mode = (ubt::Mode) juce::jlimit (0, 2, (int) pMode->load());
    p.mask = getActiveMask();
    p.retuneMs = pRetune->load();
    p.humanize = pHumanize->load() * 0.01f;
    p.formant = pFormant->load();
    p.transpose = std::round (pTranspose->load());
    p.mix = pMix->load() * 0.01f;
    p.outGain = juce::Decibels::decibelsToGain (pOutput->load());
    engine.setParams (p);

    engine.process (buffer.getArrayOfWritePointers(), numOut, buffer.getNumSamples());
}

void UBTuneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("customMask", (int) customMask.load(), nullptr);
    state.setProperty ("version", UBTUNE_VERSION_STRING, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void UBTuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            customMask = (int) vt.getProperty ("customMask", 0x0FFF);
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* UBTuneProcessor::createEditor() { return new UBTuneEditor (*this); }

#ifndef UBTUNE_SNAPSHOT
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UBTuneProcessor(); }
#endif
