// Renders the UBTune editor to PNG (website screenshots + CI smoke test).
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String out = argc > 1 ? argv[1] : "/tmp/ubtune.png";
    const juce::String scene = argc > 2 ? argv[2] : "pro";
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 2.0f;

    UBTuneProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    if (scene == "robotic") proc.applyModePreset (2);
    if (scene == "smooth") { proc.applyModePreset (0); proc.setParam (ids::key, 0); proc.setParam (ids::scale, ubt::Major); }

    std::unique_ptr<UBTuneEditor> ed (dynamic_cast<UBTuneEditor*> (proc.createEditor()));
    ed->setSize (UBTuneMain::W, UBTuneMain::H);
    auto& m = ed->getMain();
    const float sung = scene == "robotic" ? 64.3f : scene == "smooth" ? 67.2f : 57.25f;
    proc.engine.tLevel = 0.07f;
    proc.engine.tDelayMs = 2.7f;
    for (int i = 0; i < 90; ++i)
    {
        proc.engine.tInputMidi = sung + 0.18f * std::sin (i * 0.21f);
        proc.engine.tTargetNote = scene == "robotic" ? 64 : scene == "smooth" ? 67 : 57;
        proc.engine.tCorrection = -0.25f;
        m.tick (1.0f / 60.0f);
    }
    if (scene == "credits") m.showCredits (true);

    auto img = m.createComponentSnapshot (m.getLocalBounds(), true, scale);
    juce::File f (out); f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png; png.writeImageToStream (img, os);
    std::printf ("wrote %s (%dx%d)\n", out.toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
