#include "PluginEditor.h"

using namespace theme;

// ============================== Pill ==============================
void Pill::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = std::min (b.getHeight() * 0.5f, 14.0f);
    const bool hover = isMouseOver();
    if (on)
    {
        juce::ColourGradient gr (lilaDeep, b.getX(), b.getBottom(), accent ? magenta : lila, b.getRight(), b.getY(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (b, r);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (b, r, 1.0f);
    }
    else
    {
        g.setColour (hover ? card2.brighter (0.08f) : card2);
        g.fillRoundedRectangle (b, r);
        g.setColour (juce::Colours::white.withAlpha (hover ? 0.14f : 0.07f));
        g.drawRoundedRectangle (b, r, 1.0f);
    }
    g.setColour (on ? juce::Colours::white : (hover ? text : text.withAlpha (0.78f)));
    if (subtitle.isEmpty())
    {
        g.setFont (monoFont ? mono (fontSize, true) : sans (fontSize, 600));
        g.drawFittedText (title, getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
    }
    else
    {
        auto tb = getLocalBounds().reduced (8, 6);
        g.setFont (sans (15.0f, 700));
        g.drawText (title, tb.removeFromTop (tb.getHeight() / 2 + 2), juce::Justification::centredBottom);
        g.setColour (on ? juce::Colours::white.withAlpha (0.8f) : dim);
        g.setFont (mono (9.5f));
        g.drawText (subtitle, tb, juce::Justification::centredTop);
    }
}

// ============================== Knob ==============================
Knob::Knob (UBTuneProcessor& p, const juce::String& id, juce::String l, float resetValue) : label (std::move (l))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, slider);
    slider.setDoubleClickReturnValue (true, resetValue);
    slider.setPopupDisplayEnabled (false, false, nullptr);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (dim);
    g.setFont (mono (10.5f, true));
    g.drawText (label, getLocalBounds().removeFromBottom (18), juce::Justification::centred);
}

// ============================== Main ==============================
static const char* kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

UBTuneMain::UBTuneMain (UBTuneProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (ring);
    addAndMakeVisible (piano);
    piano.onToggle = [this] (int pc) { proc.toggleNoteInMask (pc); };

    const char* modeT[] = { "SMOOTH", "PRO", "T-PAIN" };
    const char* modeS[] = { "NATURAL · REAL-TIME", "CLASSIC · TIGHT", "ROBOT · HARD SNAP" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = modePills.add (new Pill (modeT[i], juce::String::fromUTF8 (modeS[i])));
        if (i == 2) b->setAccent (true);
        b->onClick = [this, i] { proc.applyModePreset (i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 12; ++i)
    {
        auto* b = keyPills.add (new Pill (kNotes[i]));
        b->fontSize = 12.5f;
        b->onClick = [this, i] { proc.setParam (ids::key, (float) i); };
        addAndMakeVisible (b);
    }
    auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (ids::scale));
    for (int i = 0; i < scaleParam->choices.size(); ++i)
    {
        auto* b = scalePills.add (new Pill (scaleParam->choices[i]));
        b->fontSize = 10.5f;
        if (i == ubt::PentMajor || i == ubt::PentMinor) b->setAccent (true);
        b->onClick = [this, i] { proc.setParam (ids::scale, (float) i); };
        addAndMakeVisible (b);
    }
    detectBtn.fontSize = 11.0f;
    detectBtn.setAccent (true);
    detectBtn.onClick = [this] {
        if (proc.engine.tDetectProgress.load() >= 0.0f) proc.engine.cancelKeyDetect();
        else proc.engine.startKeyDetect();
    };
    addAndMakeVisible (detectBtn);
    infoBtn.monoFont = false; infoBtn.fontSize = 14.0f;
    infoBtn.onClick = [this] { showCredits = ! showCredits; credits.setVisible (showCredits); };
    addAndMakeVisible (infoBtn);

    knobs.add (new Knob (proc, ids::retune, "RETUNE SPEED", 20.0f));
    knobs.add (new Knob (proc, ids::humanize, "HUMANIZE", 25.0f));
    knobs.add (new Knob (proc, ids::formant, "FORMANT", 0.0f));
    knobs.add (new Knob (proc, ids::mix, "MIX", 100.0f));
    knobs.add (new Knob (proc, ids::transpose, "TRANSPOSE", 0.0f));
    knobs.add (new Knob (proc, ids::output, "OUTPUT", 0.0f));
    for (auto* k : knobs) addAndMakeVisible (k);
    credits.onClose = [this] { showCredits = false; credits.setVisible (false); };
    addChildComponent (credits);

    lastDetectSerial = proc.engine.tDetectSerial.load();
    syncFromParams();
    setSize (W, H);
   #if ! UBTUNE_SNAPSHOT
    startTimerHz (60);
   #endif
}

UBTuneMain::~UBTuneMain() { stopTimer(); setLookAndFeel (nullptr); }

void UBTuneMain::resized()
{
    // header
    logoArea = { 24, 16, 260, 40 };
    int mx = 334;
    for (auto* b : modePills) { b->setBounds (mx, 14, 150, 44); mx += 158; }
    infoBtn.setBounds (W - 24 - 32, 20, 32, 32);

    leftCard = { 20, 76, 232, 380 };
    rightCard = { W - 20 - 232, 76, 232, 380 };
    pianoCard = { 20, 470, W - 40, 166 };

    // left: keys 4x3, scales 2 columns, detect
    auto lc = leftCard.reduced (16);
    lc.removeFromTop (22);
    auto keys = lc.removeFromTop (3 * 34);
    for (int i = 0; i < 12; ++i)
    {
        const int r = i / 4, c = i % 4;
        const int w = keys.getWidth() / 4;
        keyPills[i]->setBounds (keys.getX() + c * w, keys.getY() + r * 34, w - 4, 30);
    }
    lc.removeFromTop (12);
    lc.removeFromTop (22);
    auto sc = lc.removeFromTop (5 * 31);
    for (int i = 0; i < scalePills.size(); ++i)
    {
        const int r = i / 2, c = i % 2;
        const int w = sc.getWidth() / 2;
        scalePills[i]->setBounds (sc.getX() + c * w, sc.getY() + r * 31, w - 4, 27);
    }
    detectBtn.setBounds (lc.removeFromBottom (32).withTrimmedRight (4));

    // right: 2x2 big knobs + 2 small
    auto rc = rightCard.reduced (14);
    rc.removeFromTop (18);
    const int kw = rc.getWidth() / 2;
    for (int i = 0; i < 4; ++i)
        knobs[i]->setBounds (rc.getX() + (i % 2) * kw, rc.getY() + (i / 2) * 124, kw, 120);
    const int sy = rc.getY() + 2 * 124 + 4;
    knobs[4]->setBounds (rc.getX() + 6, sy, kw - 12, 92);
    knobs[5]->setBounds (rc.getX() + kw + 6, sy, kw - 12, 92);

    ring.setBounds (leftCard.getRight() + 6, 66, rightCard.getX() - leftCard.getRight() - 12, 400);
    piano.setBounds (pianoCard.reduced (16, 12).withTrimmedTop (16));
    credits.setBounds (getLocalBounds());
}

void UBTuneMain::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    {
        juce::ColourGradient top (lilaDeep.withAlpha (0.16f), W * 0.5f, -80.0f, bg.withAlpha (0.0f), W * 0.5f, 360.0f, true);
        g.setGradientFill (top); g.fillRect (getLocalBounds());
    }

    // logo + title
    auto logo = theme::logo();
    if (logo.isValid())
    {
        const float h = 34.0f, w = h * logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);
        g.drawImage (logo, juce::Rectangle<float> ((float) logoArea.getX(), (float) logoArea.getY() + 2, w, h), juce::RectanglePlacement::centred);
        g.setColour (text);
        g.setFont (sans (26.0f, 700));
        const float tx = logoArea.getX() + w + 10.0f;
        g.drawText ("UBTune", juce::Rectangle<float> (tx, (float) logoArea.getY() - 2, 140, 30), juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (mono (9.5f));
        g.drawText (juce::String::fromUTF8 ("BY UNIQUE & MBEZZA  ·  v" UBTUNE_VERSION_STRING), juce::Rectangle<float> (tx + 1, (float) logoArea.getY() + 26, 220, 14), juce::Justification::centredLeft);
    }

    auto drawCard = [&] (juce::Rectangle<int> r, const juce::String& eyebrow)
    {
        auto rf = r.toFloat();
        g.setColour (card.withAlpha (0.86f));
        g.fillRoundedRectangle (rf, 18.0f);
        g.setColour (line);
        g.drawRoundedRectangle (rf.reduced (0.5f), 18.0f, 1.0f);
        if (eyebrow.isNotEmpty())
        {
            g.setColour (lilaLight.withAlpha (0.9f));
            g.setFont (mono (10.0f, true));
            g.drawText (eyebrow, r.reduced (16, 12).removeFromTop (16), juce::Justification::centredLeft);
        }
    };
    drawCard (leftCard, "KEY");
    {
        g.setColour (lilaLight.withAlpha (0.9f));
        g.setFont (mono (10.0f, true));
        g.drawText ("SCALE", juce::Rectangle<int> (leftCard.getX() + 16, leftCard.getY() + 16 + 22 + 102 + 12, 200, 16), juce::Justification::centredLeft);
    }
    drawCard (rightCard, "CONTROLS");
    drawCard (pianoCard, "NOTES");
    {
        g.setColour (dim);
        g.setFont (mono (9.5f));
        g.drawText ("CLICK A KEY TO ADD / REMOVE IT FROM THE SCALE",
                    pianoCard.reduced (16, 12).removeFromTop (16), juce::Justification::centredRight);
    }

    // footer
    g.setColour (faint);
    g.setFont (mono (9.5f));
    const double lat = proc.getSampleRate() > 0 ? 1000.0 * proc.engine.getLatency() / proc.getSampleRate() : 0.0;
    g.drawText (juce::String::fromUTF8 ("UBTUNE  ·  UNIQUE & MBEZZA  ·  GELO SANTINO MARES  ·  MERON NEGASH  ·  UNIQUEBABY.DE"),
                juce::Rectangle<int> (24, H - 22, W - 220, 16), juce::Justification::centredLeft);
    g.drawText ("LATENCY " + juce::String (lat, 1) + " MS", juce::Rectangle<int> (W - 224, H - 22, 200, 16), juce::Justification::centredRight);
}

void UBTuneMain::mouseUp (const juce::MouseEvent& e)
{
    if (logoArea.contains (e.getPosition())) { showCredits = true; credits.setVisible (true); }
}

void UBTuneMain::syncFromParams()
{
    const int mode = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (ids::mode)->load());
    for (int i = 0; i < modePills.size(); ++i) modePills[i]->setOn (i == mode);
    const int key = proc.getKey(), scale = proc.getScale();
    for (int i = 0; i < keyPills.size(); ++i) keyPills[i]->setOn (i == key && scale != UBTuneProcessor::kCustomScale);
    for (int i = 0; i < scalePills.size(); ++i) scalePills[i]->setOn (i == scale);
    const bool det = proc.engine.tDetectProgress.load() >= 0.0f;
    detectBtn.setOn (det);
    detectBtn.setTitle (det ? "LISTENING ... (STOP)" : "DETECT KEY");
    infoBtn.setOn (showCredits);
}

void UBTuneMain::tick (float dt)
{
    syncFromParams();

    const int serial = proc.engine.tDetectSerial.load();
    if (serial != lastDetectSerial)
    {
        lastDetectSerial = serial;
        proc.applyDetectedKey (proc.engine.tDetectedKey.load(), proc.engine.tDetectedMinor.load() != 0);
    }

    ParticleRing::State s;
    s.level = proc.engine.tLevel.load();
    s.inputMidi = proc.engine.tInputMidi.load();
    s.targetNote = proc.engine.tTargetNote.load();
    s.correction = proc.engine.tCorrection.load();
    s.key = proc.getKey();
    const int sc = proc.getScale();
    s.scaleName = sc == UBTuneProcessor::kCustomScale ? juce::String ("CUSTOM") : juce::String (ubt::scaleName (sc));
    s.mask = proc.getActiveMask();
    s.mode = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (ids::mode)->load());
    s.detectProgress = proc.engine.tDetectProgress.load();
    ring.setState (s);
    ring.advance (dt);
    piano.setState (s.inputMidi, s.targetNote, s.mask, s.key);
    piano.advance (dt);
}

void UBTuneMain::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = lastTime > 0 ? (float) juce::jlimit (0.0, 0.1, now - lastTime) : 1.0f / 60.0f;
    lastTime = now;
    tick (dt);
    ring.repaint();
    piano.repaint();
}

// ============================== Credits ==============================
void CreditsPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.88f));
    auto r = getLocalBounds().withSizeKeepingCentre (540, 362).toFloat();
    juce::ColourGradient gr (card2, r.getX(), r.getY(), card, r.getRight(), r.getBottom(), false);
    g.setGradientFill (gr); g.fillRoundedRectangle (r, 26.0f);
    for (int i = 1; i <= 3; ++i) { g.setColour (lila.withAlpha (0.06f)); g.drawRoundedRectangle (r.expanded ((float) i * 3), 26.0f + i * 3, 3.0f); }
    g.setColour (lila.withAlpha (0.5f)); g.drawRoundedRectangle (r, 26.0f, 1.0f);

    auto ri = r.toNearestInt().reduced (40, 34);
    auto logo = theme::logo();
    g.setOpacity (1.0f);
    g.drawImage (logo, ri.removeFromTop (62).toFloat(), juce::RectanglePlacement::centred);
    ri.removeFromTop (8);
    g.setColour (text); g.setFont (sans (40.0f, 700));
    g.drawText ("UBTune", ri.removeFromTop (46), juce::Justification::centred);
    g.setColour (dim); g.setFont (mono (11.0f));
   #if JUCE_MAC
    const char* fmts = "VERSION " UBTUNE_VERSION_STRING "  ·  VST3 · AU · STANDALONE";
   #else
    const char* fmts = "VERSION " UBTUNE_VERSION_STRING "  ·  VST3 · STANDALONE";
   #endif
    g.drawText (juce::String::fromUTF8 (fmts), ri.removeFromTop (18), juce::Justification::centred);
    ri.removeFromTop (22);
    g.setColour (lilaLight); g.setFont (mono (10.5f, true));
    g.drawText ("ENTWICKELT VON", ri.removeFromTop (16), juce::Justification::centred);
    g.setColour (text); g.setFont (sans (26.0f, 700));
    g.drawText ("unique & Mbezza", ri.removeFromTop (34), juce::Justification::centred);
    g.setColour (text.withAlpha (0.8f)); g.setFont (sans (15.0f));
    g.drawText (juce::String::fromUTF8 ("Gelo Santino Mares  ·  Meron Negash"), ri.removeFromTop (22), juce::Justification::centred);
    ri.removeFromTop (22);
    g.setFont (mono (11.0f, true));
    auto row = ri.removeFromTop (18);
    const int w = row.getWidth() / 3;
    linkSite = row.removeFromLeft (w); linkUb = row.removeFromLeft (w); linkIg = row;
    g.setColour (lila);
    g.drawText ("UBTUNE.UNIQUEBABY.DE", linkSite, juce::Justification::centred);
    g.drawText ("UNIQUEBABY.DE", linkUb, juce::Justification::centred);
    g.drawText ("@GELOUNIQUE", linkIg, juce::Justification::centred);
    ri.removeFromTop (18);
    g.setColour (faint); g.setFont (mono (9.5f));
    g.drawText (juce::String::fromUTF8 ("(C) 2026 UNIQUEBABY  ·  KLICK ZUM SCHLIESSEN"), ri.removeFromTop (14), juce::Justification::centred);
}

void CreditsPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (linkSite.contains (p)) juce::URL ("https://ubtune.uniquebaby.de").launchInDefaultBrowser();
    else if (linkUb.contains (p)) juce::URL ("https://uniquebaby.de").launchInDefaultBrowser();
    else if (linkIg.contains (p)) juce::URL ("https://instagram.com/gelounique").launchInDefaultBrowser();
    else if (onClose) onClose();
}

// ============================== Editor ==============================
UBTuneEditor::UBTuneEditor (UBTuneProcessor& p) : AudioProcessorEditor (p), main (p)
{
    addAndMakeVisible (main);
    setResizable (true, true);
    setResizeLimits (UBTuneMain::W * 6 / 10, UBTuneMain::H * 6 / 10, UBTuneMain::W * 2, UBTuneMain::H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) UBTuneMain::W / UBTuneMain::H);
    setSize (UBTuneMain::W, UBTuneMain::H);
   #if ! UBTUNE_SNAPSHOT
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);
    gl.attachTo (*this);
   #endif
}

UBTuneEditor::~UBTuneEditor()
{
   #if ! UBTUNE_SNAPSHOT
    gl.detach();
   #endif
}

void UBTuneEditor::resized()
{
    const float s = (float) getWidth() / (float) UBTuneMain::W;
    main.setTransform (juce::AffineTransform::scale (s));
    main.setBounds (0, 0, UBTuneMain::W, UBTuneMain::H);
}
