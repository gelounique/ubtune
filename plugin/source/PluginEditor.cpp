#include "PluginEditor.h"

using namespace theme;

static const char* kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

// ============================== RectButton ==============================
void RectButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const bool hover = isMouseOver();
    if (on)
    {
        g.setColour (text);
        g.fillRoundedRectangle (b, 9.0f);
    }
    else if (hover)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (b, 9.0f);
    }
    g.setColour (on ? bg : (hover ? text : dim));
    g.setFont (sans (fontSize, 700));
    g.drawFittedText (title, getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

// ============================== Selector ==============================
void Selector::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (card2); g.fillRoundedRectangle (b, radius);
    g.setColour (line); g.drawRoundedRectangle (b, radius, 1.0f);

    auto arrow = [&] (bool left)
    {
        auto z = left ? b.withWidth (40.0f) : b.withLeft (b.getRight() - 40.0f);
        const bool hv = hoverZone == (left ? -1 : 1);
        if (hv) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRoundedRectangle (z.reduced (5.0f), 8.0f); }
        juce::Path p;
        const auto c = z.getCentre();
        if (left) p.addTriangle (c.x + 3, c.y - 6, c.x + 3, c.y + 6, c.x - 4, c.y);
        else      p.addTriangle (c.x - 3, c.y - 6, c.x - 3, c.y + 6, c.x + 4, c.y);
        g.setColour (hv ? text : dim);
        g.fillPath (p);
    };
    arrow (true); arrow (false);

    auto mid = getLocalBounds().reduced (40, 7);
    g.setColour (dim);
    g.setFont (mono (9.5f, true));
    g.drawText (lbl, mid.removeFromTop (14), juce::Justification::centred);
    g.setColour (hoverZone == 0 ? juce::Colours::white : text);
    const juce::String v = isCustom ? customText : opts[idx];
    g.setFont (sans (v.length() > 3 ? juce::jmin (valueSize, 15.0f) : valueSize, 700));
    g.drawFittedText (v, mid, juce::Justification::centred, 2);
}

void Selector::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    const int z = zoneAt (e.x);
    if (z != 0)
    {
        const int ni = ((isCustom ? idx : idx + z) % opts.size() + opts.size()) % opts.size();
        if (onPick) onPick (ni);
        return;
    }
    juce::PopupMenu m;
    m.setLookAndFeel (&getLookAndFeel());
    for (int i = 0; i < opts.size(); ++i) m.addItem (i + 1, opts[i], true, ! isCustom && i == idx);
    juce::Component::SafePointer<Selector> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (30),
                     [safe] (int r) { if (safe != nullptr && r > 0 && safe->onPick) safe->onPick (r - 1); });
}

// ============================== DetectButton ==============================
void DetectButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool listening = prog >= 0.0f;
    const bool hover = isMouseOver();
    if (! listening)
    {
        g.setColour (hover ? juce::Colours::white : text);
        g.fillRoundedRectangle (b, radius);
    }
    else
    {
        g.setColour (card2); g.fillRoundedRectangle (b, radius);
        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (b, radius);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        const float hFill = b.getHeight() * juce::jlimit (0.02f, 1.0f, prog);
        g.fillRect (b.withTop (b.getBottom() - hFill));
        g.restoreState();
        g.setColour (text.withAlpha (0.6f)); g.drawRoundedRectangle (b, radius, 1.0f);
    }
    const juce::Colour fg = listening ? text : bg;

    // icon: dot + sound waves
    const auto ic = juce::Point<float> (b.getCentreX(), b.getY() + b.getHeight() * 0.32f);
    g.setColour (fg);
    g.fillEllipse (ic.x - 6, ic.y - 6, 12, 12);
    for (int i = 1; i <= 3; ++i)
    {
        juce::Path a1, a2;
        const float rr = 8.0f + i * 8.0f;
        a1.addCentredArc (ic.x, ic.y, rr, rr, 0, -2.25f, -0.9f, true);
        a2.addCentredArc (ic.x, ic.y, rr, rr, 0, 0.9f, 2.25f, true);
        const float pulse = listening ? 0.25f + 0.75f * (0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f - i * 1.2f)) : (1.0f - 0.2f * i);
        g.setColour (fg.withAlpha (pulse));
        g.strokePath (a1, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (a2, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    auto tr = getLocalBounds().reduced (8, 0).withTop ((int) (b.getY() + b.getHeight() * 0.56f));
    g.setColour (fg);
    g.setFont (sans (17.0f, 700));
    g.drawFittedText (listening ? juce::String ((int) std::round (prog * 100.0f)) + " %" : juce::String ("DETECT\nKEY"),
                      tr.removeFromTop (44), juce::Justification::centred, 2);
    g.setColour (fg.withAlpha (0.6f));
    g.setFont (mono (9.0f, true));
    g.drawFittedText (listening ? U ("SING WEITER\nKLICK = STOPP") : (result.isNotEmpty() ? "ERKANNT\n" + result : U ("8 SEK SINGEN")),
                      tr.removeFromTop (30), juce::Justification::centred, 2);
}

// ============================== Stepper ==============================
Stepper::Stepper (UBTuneProcessor& p, const juce::String& id, juce::String label) : proc (p), lbl (std::move (label))
{
    param = p.apvts.getParameter (id);
    param->addListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
Stepper::~Stepper() { param->removeListener (this); cancelPendingUpdate(); }

void Stepper::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (card2); g.fillRoundedRectangle (b, 9.0f);
    g.setColour (dim);
    g.setFont (sans (20.0f, 600));
    g.drawText (U ("−"), getLocalBounds().removeFromLeft (34), juce::Justification::centred);
    g.drawText ("+", getLocalBounds().removeFromRight (34), juce::Justification::centred);
    auto mid = getLocalBounds().reduced (34, 0);
    g.setFont (mono (9.5f, true));
    g.drawText (lbl, mid.removeFromLeft (mid.getWidth() / 2 + 6), juce::Justification::centredLeft);
    const float v = param->convertFrom0to1 (param->getValue());
    g.setColour (std::abs (v) > 0.5f ? juce::Colours::white : text.withAlpha (0.85f));
    g.setFont (mono (14.0f, true));
    g.drawText (param->getCurrentValueAsText(), mid, juce::Justification::centredRight);
}

void Stepper::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked() || e.getNumberOfClicks() > 1) return;
    if (e.x > 34 && e.x < getWidth() - 34) return;
    const float v = param->convertFrom0to1 (param->getValue());
    const float d = e.x < getWidth() / 2 ? -1.0f : 1.0f;
    proc.setParam (param->getParameterID(), juce::jlimit (-12.0f, 12.0f, v + d));
}

void Stepper::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x > 34 && e.x < getWidth() - 34) proc.setParam (param->getParameterID(), 0.0f);
}

// ============================== Knob ==============================
Knob::Knob (UBTuneProcessor& p, const juce::String& id, juce::String l, float resetValue, int sz) : label (std::move (l)), size (sz)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    slider.getProperties().set ("ubSize", sz);
    slider.getProperties().set ("ubLabel", label);
    slider.setMouseDragSensitivity (sz == 2 ? 320 : 220);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, id, slider);
    slider.setDoubleClickReturnValue (true, resetValue);
}

void Knob::resized() { slider.setBounds (getLocalBounds()); }
void Knob::paint (juce::Graphics&) {}

// ============================== Credits ==============================
void CreditsPanel::paint (juce::Graphics& g)
{
    g.fillAll (bg.withAlpha (0.92f));
    auto r = getLocalBounds().withSizeKeepingCentre (540, 372).toFloat();
    g.setColour (card); g.fillRoundedRectangle (r, 22.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f)); g.drawRoundedRectangle (r, 22.0f, 1.0f);

    auto ri = r.toNearestInt().reduced (40, 34);
    g.setOpacity (1.0f);
    g.drawImage (theme::logo(), ri.removeFromTop (58).toFloat(), juce::RectanglePlacement::centred);
    ri.removeFromTop (8);
    g.setColour (text); g.setFont (sans (40.0f, 700));
    g.drawText ("UBTune", ri.removeFromTop (46), juce::Justification::centred);
   #if JUCE_MAC
    const char* fmts = "VERSION " UBTUNE_VERSION_STRING "  ·  VST3 · AU · STANDALONE";
   #else
    const char* fmts = "VERSION " UBTUNE_VERSION_STRING "  ·  VST3 · STANDALONE";
   #endif
    g.setColour (dim); g.setFont (mono (11.0f));
    g.drawText (U (fmts), ri.removeFromTop (18), juce::Justification::centred);
    ri.removeFromTop (22);
    g.setColour (dim); g.setFont (mono (10.5f, true));
    g.drawText ("ENTWICKELT VON", ri.removeFromTop (16), juce::Justification::centred);
    g.setColour (text); g.setFont (sans (26.0f, 700));
    g.drawText ("unique & Mbezza", ri.removeFromTop (34), juce::Justification::centred);
    g.setColour (text.withAlpha (0.8f)); g.setFont (sans (15.0f));
    g.drawText (U ("Gelo Santino Mares  ·  Meron Negash"), ri.removeFromTop (22), juce::Justification::centred);
    ri.removeFromTop (22);
    g.setFont (mono (11.0f, true));
    auto row = ri.removeFromTop (18);
    const int w = row.getWidth() / 3;
    linkSite = row.removeFromLeft (w); linkUb = row.removeFromLeft (w); linkIg = row;
    g.setColour (text);
    g.drawText ("UBTUNE.UNIQUEBABY.DE", linkSite, juce::Justification::centred);
    g.drawText ("UNIQUEBABY.DE", linkUb, juce::Justification::centred);
    g.drawText ("@GELOUNIQUE", linkIg, juce::Justification::centred);
    ri.removeFromTop (16);
    g.setColour (faint); g.setFont (mono (9.5f));
    g.drawText (U ("© 2026 UNIQUEBABY  ·  KLICK ZUM SCHLIESSEN"), ri.removeFromTop (14), juce::Justification::centred);
}

void CreditsPanel::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (linkSite.contains (p)) juce::URL ("https://ubtune.uniquebaby.de").launchInDefaultBrowser();
    else if (linkUb.contains (p)) juce::URL ("https://uniquebaby.de").launchInDefaultBrowser();
    else if (linkIg.contains (p)) juce::URL ("https://instagram.com/gelounique").launchInDefaultBrowser();
    else if (onClose) onClose();
}

// ============================== Main ==============================
static juce::StringArray scaleNames()
{
    juce::StringArray s;
    for (int i = 0; i < ubt::NumScales; ++i) s.add (ubt::scaleName (i));
    return s;
}

UBTuneMain::UBTuneMain (UBTuneProcessor& p)
    : proc (p),
      keySel ("KEY", juce::StringArray (kNotes, 12)),
      scaleSel ("SCALE", scaleNames()),
      transpose (p, ids::transpose, "TRANSPOSE")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (ring);
    addAndMakeVisible (piano);
    piano.onToggle = [this] (int pc) { proc.toggleNoteInMask (pc); };

    const char* modeT[] = { "SMOOTH", "PRO", "ROBOTIC" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = modeBtns.add (new RectButton (modeT[i]));
        b->onClick = [this, i] { proc.applyModePreset (i); };
        addAndMakeVisible (b);
    }

    keySel.onPick = [this] (int i)
    {
        proc.setParam (ids::key, (float) i);
        if (proc.getScale() == UBTuneProcessor::kCustomScale) proc.setParam (ids::scale, (float) ubt::PentMinor);
    };
    scaleSel.onPick = [this] (int i) { proc.setParam (ids::scale, (float) i); };
    keySel.valueSize = 58.0f;
    scaleSel.valueSize = 15.0f;
    addAndMakeVisible (keySel);
    addAndMakeVisible (scaleSel);
    addAndMakeVisible (transpose);

    detectBtn.onClick = [this] {
        if (proc.engine.tDetectProgress.load() >= 0.0f) proc.engine.cancelKeyDetect();
        else proc.engine.startKeyDetect();
    };
    addAndMakeVisible (detectBtn);

    infoBtn.fontSize = 15.0f;
    infoBtn.onClick = [this] { credits.setVisible (! credits.isVisible()); };
    addAndMakeVisible (infoBtn);

    retune   = std::make_unique<Knob> (proc, ids::retune, "RETUNE SPEED", 20.0f, 2);
    humanize = std::make_unique<Knob> (proc, ids::humanize, "HUMANIZE", 25.0f, 1);
    formant  = std::make_unique<Knob> (proc, ids::formant, "FORMANT", 0.0f, 1);
    mix      = std::make_unique<Knob> (proc, ids::mix, "MIX", 100.0f, 0);
    for (auto* k : { retune.get(), humanize.get(), formant.get(), mix.get() }) addAndMakeVisible (k);

    output.setSliderStyle (juce::Slider::LinearHorizontal);
    output.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outputAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids::output, output);
    output.setDoubleClickReturnValue (true, 0.0);
    output.onValueChange = [this] { repaint (outputArea); };
    addAndMakeVisible (output);

    credits.onClose = [this] { credits.setVisible (false); };
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
    logoArea = { 20, 8, 260, 40 };
    const int segW = 390, segX = (W - segW) / 2;
    for (int i = 0; i < 3; ++i) modeBtns[i]->setBounds (segX + 4 + i * 128, 10, 126, 32);
    infoBtn.setBounds (W - 20 - 32, 10, 32, 32);

    // stage: orb left, knob cluster on a curved panel right (MetaTune layout)
    ring.setBounds (12, 56, 600, 400);
    knobPanel.clear();
    knobPanel.startNewSubPath (700.0f, 52.0f);
    knobPanel.lineTo ((float) W, 52.0f);
    knobPanel.lineTo ((float) W, 458.0f);
    knobPanel.lineTo (610.0f, 458.0f);
    knobPanel.cubicTo (650.0f, 400.0f, 600.0f, 160.0f, 700.0f, 52.0f);
    knobPanel.closeSubPath();

    retune->setBounds   (650, 64, 236, 236);
    mix->setBounds      (884, 96, 100, 100);
    humanize->setBounds (706, 292, 162, 162);
    formant->setBounds  (872, 316, 118, 118);

    // control bar
    barArea = { 12, 466, W - 24, 40 };
    transpose.setBounds (barArea.getX() + 4, barArea.getY() + 4, 210, 32);
    outputArea = { barArea.getX() + 230, barArea.getY() + 4, 260, 32 };
    output.setBounds (outputArea.withTrimmedLeft (74).withTrimmedRight (70).withSizeKeepingCentre (outputArea.getWidth() - 144, 22));
    latencyArea = { barArea.getRight() - 230, barArea.getY(), 222, 40 };

    // bottom: key/scale | heatmaps + keyboard | detect
    keySel.setBounds (12, 514, 150, 114);
    scaleSel.setBounds (12, 634, 150, 58);
    piano.setBounds (174, 514, W - 174 - 152, 178);
    detectBtn.setBounds (W - 140, 514, 128, 178);
    credits.setBounds (getLocalBounds());
}

void UBTuneMain::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    // header strip
    g.setColour (juce::Colour (0xff0b0b0d)); g.fillRect (0, 0, W, 52);
    g.setColour (line); g.fillRect (0, 52, W, 1);
    auto logo = theme::logo();
    if (logo.isValid())
    {
        const float h = 28.0f, w = h * logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);
        g.drawImage (logo, juce::Rectangle<float> ((float) logoArea.getX(), (float) logoArea.getY() + 4, w, h), juce::RectanglePlacement::centred);
        const float tx = logoArea.getX() + w + 10.0f;
        g.setColour (text); g.setFont (sans (22.0f, 700));
        g.drawText ("UBTune", juce::Rectangle<float> (tx, (float) logoArea.getY() + 1, 140, 24), juce::Justification::centredLeft);
        g.setColour (dim); g.setFont (mono (9.0f));
        g.drawText (U ("BY UNIQUE & MBEZZA  ·  v" UBTUNE_VERSION_STRING), juce::Rectangle<float> (tx + 1, (float) logoArea.getY() + 24, 220, 12), juce::Justification::centredLeft);
    }
    {
        const int segW = 390, segX = (W - segW) / 2;
        auto r = juce::Rectangle<float> ((float) segX, 7.0f, (float) segW, 38.0f);
        g.setColour (card2); g.fillRoundedRectangle (r, 11.0f);
    }

    // curved knob panel (graphite)
    {
        juce::ColourGradient gr (juce::Colour (0xff1a1a1d), 900.0f, 60.0f, juce::Colour (0xff0d0d0f), 700.0f, 450.0f, false);
        g.setGradientFill (gr);
        g.fillPath (knobPanel);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.strokePath (knobPanel, juce::PathStrokeType (1.0f));
    }

    // control bar
    g.setColour (card); g.fillRoundedRectangle (barArea.toFloat(), 12.0f);
    g.setColour (line); g.drawRoundedRectangle (barArea.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.setColour (dim); g.setFont (mono (9.5f, true));
    g.drawText ("OUTPUT", outputArea.withWidth (70).withTrimmedLeft (12), juce::Justification::centredLeft);
    g.setColour (text); g.setFont (mono (12.0f, true));
    g.drawText (proc.apvts.getParameter (ids::output)->getCurrentValueAsText(), outputArea.withTrimmedLeft (outputArea.getWidth() - 68), juce::Justification::centredRight);
    g.setColour (faint); g.setFont (mono (9.5f, true));
    g.drawText (U ("TASTE KLICKEN = NOTE AN / AUS"), juce::Rectangle<int> (outputArea.getRight() + 20, barArea.getY(), 240, 40), juce::Justification::centredLeft);
    {
        // click = toggle LOW LATENCY (tight, ~half delay) <-> CLEAN (wider window, max. smooth on wobbly voices)
        const double ms = proc.engine.tDelayMs.load();
        const bool low = proc.apvts.getRawParameterValue (ids::lowLatency)->load() > 0.5f;
        g.setColour (latencyHover ? line.brighter (0.4f) : line);
        g.drawRoundedRectangle (latencyArea.toFloat().reduced (2.0f, 7.0f), 13.0f, 1.0f);
        const juce::Rectangle<float> dot ((float) latencyArea.getX() + 10, (float) latencyArea.getCentreY() - 4, 8, 8);
        g.setColour (juce::Colours::white);
        if (low) g.fillEllipse (dot); else g.drawEllipse (dot.reduced (0.5f), 1.2f);
        g.setColour (latencyHover ? text : dim); g.setFont (mono (9.5f, true));
        g.drawText (low ? "LOW LATENCY" : "CLEAN MODE", latencyArea.withTrimmedLeft (26), juce::Justification::centredLeft);
        g.setColour (text); g.setFont (mono (13.0f, true));
        g.drawText ((ms > 0.05 ? juce::String (ms, 1) : juce::String ("< 2")) + " MS", latencyArea.withTrimmedRight (14), juce::Justification::centredRight);
    }

}

void UBTuneMain::paintOverChildren (juce::Graphics& g)
{
    auto pb = piano.getBounds();
    g.setColour (dim.withAlpha (0.8f)); g.setFont (mono (8.5f, true));
    g.drawText ("IN", juce::Rectangle<int> (pb.getX() + 8, pb.getY(), 60, 16), juce::Justification::centredLeft);
    g.drawText ("OUT", juce::Rectangle<int> (pb.getX() + 8, pb.getY() + 20, 60, 16), juce::Justification::centredLeft);
}

void UBTuneMain::mouseUp (const juce::MouseEvent& e)
{
    if (logoArea.contains (e.getPosition())) credits.setVisible (true);
    if (latencyArea.contains (e.getPosition()) && e.mouseWasClicked())
    {
        auto* p = proc.apvts.getParameter (ids::lowLatency);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
        repaint (latencyArea);
    }
}

void UBTuneMain::mouseMove (const juce::MouseEvent& e)
{
    const bool h = latencyArea.contains (e.getPosition());
    if (h != latencyHover) { latencyHover = h; setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint (latencyArea); }
}

void UBTuneMain::syncFromParams()
{
    const int mode = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue (ids::mode)->load());
    for (int i = 0; i < modeBtns.size(); ++i) modeBtns[i]->setOn (i == mode);
    const int key = proc.getKey(), scale = proc.getScale();
    const bool custom = scale == UBTuneProcessor::kCustomScale;
    keySel.setIndex (key);
    scaleSel.setIndex (custom ? 0 : scale, custom);
    detectBtn.setState (proc.engine.tDetectProgress.load(), lastDetected);
    infoBtn.setOn (credits.isVisible());
}

void UBTuneMain::tick (float dt)
{
    const int serial = proc.engine.tDetectSerial.load();
    if (serial != lastDetectSerial)
    {
        lastDetectSerial = serial;
        const int k = proc.engine.tDetectedKey.load();
        const bool minor = proc.engine.tDetectedMinor.load() != 0;
        proc.applyDetectedKey (k, minor);
        lastDetected = juce::String (kNotes[(k + 12) % 12]) + (minor ? " MINOR" : " MAJOR");
    }
    syncFromParams();

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
    piano.setState (s.inputMidi, s.targetNote, s.mask, s.key, s.level);
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
    if (((int) (now * 4.0)) % 2 == 0) repaint (latencyArea);
    if (proc.engine.tDetectProgress.load() >= 0.0f) detectBtn.repaint();
}

// ============================== Editor ==============================
UBTuneEditor::UBTuneEditor (UBTuneProcessor& p) : AudioProcessorEditor (p), main (p)
{
    addAndMakeVisible (main);
    setResizable (true, true);
    setResizeLimits (UBTuneMain::W * 6 / 10, UBTuneMain::H * 6 / 10, UBTuneMain::W * 2, UBTuneMain::H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) UBTuneMain::W / UBTuneMain::H);
    setSize (UBTuneMain::W, UBTuneMain::H);
}

void UBTuneEditor::resized()
{
    const float s = (float) getWidth() / (float) UBTuneMain::W;
    main.setTransform (juce::AffineTransform::scale (s));
    main.setBounds (0, 0, UBTuneMain::W, UBTuneMain::H);
}
