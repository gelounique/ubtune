// Editor-automation style stress test: random params from a worker thread while audio runs and the UI ticks.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <random>
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        UBTuneProcessor proc;
        proc.setRateAndBufferSizeDetails (sr, 256);
        proc.prepareToPlay (sr, 256);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto& main = dynamic_cast<UBTuneEditor*> (ed.get())->getMain();
        std::atomic<bool> run { true };
        std::thread t ([&] {
            std::mt19937 rng (1); std::uniform_real_distribution<float> u (0, 1);
            juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi; double ph = 0;
            auto& params = proc.getParameters();
            for (int b = 0; run && b < 4000; ++b)
            {
                for (auto* p : params) if (u (rng) < 0.3f) p->setValueNotifyingHost (u (rng));
                const int n = 1 + (int) (u (rng) * 255);
                for (int i = 0; i < n; ++i) { float v = 0.3f * (float) std::sin (ph); ph += 2 * juce::MathConstants<double>::pi * (180 + 200 * u (rng)) / sr; buf.setSample (0, i, v); buf.setSample (1, i, v); }
                juce::AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 2, n);
                proc.processBlock (sub, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) if (! std::isfinite (sub.getSample (c, i))) { std::printf ("NaN!\n"); std::abort(); }
            }
            run = false;
        });
        for (int f = 0; run; ++f) { main.tick (1.0f / 60.0f); auto img = main.createComponentSnapshot (main.getLocalBounds(), true, 0.5f); juce::Thread::sleep (2); }
        t.join();
        ed.reset();
        std::printf ("sr %.0f ok\n", sr);
    }
    return 0;
}
