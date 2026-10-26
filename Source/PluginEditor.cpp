#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15161a };
const juce::Colour panel { 0xff202228 };
const juce::Colour grid { 0xff2e3038 };
const juce::Colour accent { 0xff7c5cff };
const juce::Colour noiseColour { 0xffff9f43 };
const juce::Colour learnColour { 0xffff4d5e };
const juce::Colour text { 0xffe8e8ee };
const juce::Colour subtle { 0xff9a9aa8 };

constexpr double MIN_FREQ = 20.0, MAX_FREQ = 20000.0;
constexpr float MIN_DB = -110.0f, MAX_DB = -10.0f;
constexpr int NUM_POINTS = 240; // display resolution of the curves

// A full-scale sine through the Hann-windowed FFT peaks at |X| = N/4.
const double FULL_SCALE_POWER = (mcd::FRAME_SIZE / 4.0) * (mcd::FRAME_SIZE / 4.0);
} // namespace

//==============================================================================
SpectrumView::SpectrumView (DenoiseProcessor& p)
    : processor (p),
      inputDb (NUM_POINTS, MIN_DB), outputDb (NUM_POINTS, MIN_DB), thresholdDb (NUM_POINTS, MIN_DB)
{
    startTimerHz (30);
}

float SpectrumView::freqToX (double freq, float width)
{
    return width * (float) (std::log (freq / MIN_FREQ) / std::log (MAX_FREQ / MIN_FREQ));
}

void SpectrumView::resized() {}

void SpectrumView::timerCallback()
{
    if (! processor.denoiser.getAnalysis (analysis))
        return;

    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    hasThreshold = analysis.hasThreshold;

    auto toDb = [] (double power) { return (float) juce::jlimit ((double) MIN_DB - 10.0, 0.0, 10.0 * std::log10 (power / FULL_SCALE_POWER + 1e-15)); };

    for (int i = 0; i < NUM_POINTS; ++i)
    {
        // Each display point covers a log-spaced frequency slice; take the strongest bin in it.
        const double f0 = MIN_FREQ * std::pow (MAX_FREQ / MIN_FREQ, (double) i / NUM_POINTS);
        const double f1 = MIN_FREQ * std::pow (MAX_FREQ / MIN_FREQ, (double) (i + 1) / NUM_POINTS);
        const int b0 = juce::jlimit (1, mcd::NUM_BINS - 1, (int) std::floor (f0 / sr * mcd::FRAME_SIZE));
        const int b1 = juce::jlimit (b0, mcd::NUM_BINS - 1, (int) std::ceil (f1 / sr * mcd::FRAME_SIZE));

        double in = 0.0, out = 0.0, thr = 0.0;
        for (int b = b0; b <= b1; ++b)
        {
            in = std::max (in, (double) analysis.input[(size_t) b]);
            out = std::max (out, (double) analysis.output[(size_t) b]);
            thr = std::max (thr, (double) analysis.threshold[(size_t) b]);
        }

        // Fast rise, slower fall, so the display is readable.
        auto ballistic = [] (float& shown, float target) { shown = target > shown ? target : shown * 0.75f + target * 0.25f; };
        ballistic (inputDb[(size_t) i], toDb (in));
        ballistic (outputDb[(size_t) i], toDb (out));
        thresholdDb[(size_t) i] = thresholdDb[(size_t) i] * 0.8f + toDb (thr) * 0.2f;
    }
    repaint();
}

juce::Path SpectrumView::makeCurve (const std::vector<float>& db, bool closed) const
{
    const auto w = (float) getWidth(), h = (float) getHeight();
    auto y = [&] (float v) { return juce::jmap (juce::jlimit (MIN_DB, MAX_DB, v), MIN_DB, MAX_DB, h, 0.0f); };

    juce::Path path;
    path.startNewSubPath (0.0f, y (db[0]));
    for (int i = 1; i < NUM_POINTS; ++i)
        path.lineTo (w * (i + 0.5f) / NUM_POINTS, y (db[(size_t) i]));
    if (closed)
    {
        path.lineTo (w, h);
        path.lineTo (0.0f, h);
        path.closeSubPath();
    }
    return path;
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth(), h = (float) getHeight();
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Grid.
    g.setFont (juce::FontOptions (10.5f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = freqToX (f, w);
        g.setColour (grid);
        g.drawVerticalLine ((int) x, 0.0f, h);
        g.setColour (subtle);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                    (int) x + 3, (int) h - 15, 40, 12, juce::Justification::left);
    }
    for (float db = -100.0f; db <= -20.0f; db += 20.0f)
    {
        g.setColour (grid);
        g.drawHorizontalLine ((int) juce::jmap (db, MIN_DB, MAX_DB, h, 0.0f), 0.0f, w);
    }

    // Band centres (the sliders below line up with these).
    g.setColour (accent.withAlpha (0.25f));
    for (double f : mcd::BAND_FREQS)
        g.drawVerticalLine ((int) freqToX (f, w), 0.0f, h);

    g.setColour (subtle.withAlpha (0.28f));
    g.fillPath (makeCurve (inputDb, true));

    if (hasThreshold)
    {
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, makeCurve (thresholdDb, false), dashes, 2);
        g.setColour (noiseColour);
        g.fillPath (dashed);
    }

    g.setColour (accent);
    g.strokePath (makeCurve (outputDb, false), juce::PathStrokeType (1.8f));

    g.setFont (juce::FontOptions (10.5f));
    for (float db = -100.0f; db <= -20.0f; db += 20.0f)
    {
        const auto label = juce::Rectangle<float> (4.0f, juce::jmap (db, MIN_DB, MAX_DB, h, 0.0f) - 14.0f, 44.0f, 13.0f);
        g.setColour (panel.withAlpha (0.8f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (subtle);
        g.drawText (juce::String ((int) db) + " dB", label, juce::Justification::centred);
    }

    // Legend.
    auto legend = [&] (int x, juce::Colour c, const juce::String& name) {
        g.setColour (c);
        g.fillRect (x, 10, 14, 3);
        g.setColour (text);
        g.drawText (name, x + 19, 4, 120, 14, juce::Justification::left);
    };
    const int lx = getWidth() - 330;
    legend (lx, subtle, "Input");
    legend (lx + 80, noiseColour, "Noise threshold");
    legend (lx + 210, accent, "Output");
}

//==============================================================================
DenoiseEditor::DenoiseEditor (DenoiseProcessor& p)
    : AudioProcessorEditor (p), processor (p), spectrum (p)
{
    lookAndFeel.setColourScheme ({ background, panel, panel, subtle, text, accent, text, accent, text });
    lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lookAndFeel.setColour (juce::Slider::trackColourId, accent);
    lookAndFeel.setColour (juce::Slider::backgroundColourId, grid);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, panel);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, grid);
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (spectrum);

    modeBox.addItemList ({ "Auto", "Learned" }, 1);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<ComboAttachment> (p.params, "mode", modeBox);

    learnButton.setClickingTogglesState (true);
    learnButton.setColour (juce::TextButton::buttonOnColourId, learnColour);
    learnButton.onClick = [this] { learnClicked(); };
    addAndMakeVisible (learnButton);

    profileStatus.setColour (juce::Label::textColourId, subtle);
    profileStatus.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (profileStatus);

    addAndMakeVisible (listenButton);
    listenAttachment = std::make_unique<ButtonAttachment> (p.params, "noiseOnly", listenButton);

    const char* knobIds[] = { "threshold", "reduction", "smoothing" };
    const char* knobNames[] = { "THRESHOLD", "REDUCTION", "SMOOTHING" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
        k.slider.setTextValueSuffix (i == 2 ? " %" : " dB");
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, grid);
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<SliderAttachment> (p.params, knobIds[i], k.slider);
        k.label.setText (knobNames[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, subtle);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (k.label);
    }

    for (int b = 0; b < mcd::NUM_BANDS; ++b)
    {
        auto& k = bandSliders[(size_t) b];
        k.slider.setSliderStyle (juce::Slider::LinearBarVertical);
        k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.slider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.75f));
        k.slider.setPopupDisplayEnabled (true, true, this);
        k.slider.setTooltip ("How much of the Reduction to apply around " + DenoiseProcessor::bandName (b) + " Hz");
        addAndMakeVisible (k.slider);
        k.attachment = std::make_unique<SliderAttachment> (p.params, DenoiseProcessor::bandParamId (b), k.slider);
        k.slider.onValueChange = [this] { repaint (bandArea); };
        k.label.setText (DenoiseProcessor::bandName (b), juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, text);
        k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (k.label);
    }

    setSize (860, 660);
    startTimerHz (8);
    timerCallback();
}

DenoiseEditor::~DenoiseEditor()
{
    processor.learning = false;
    setLookAndFeel (nullptr);
}

void DenoiseEditor::learnClicked()
{
    const bool on = learnButton.getToggleState();
    processor.learning = on;
    if (! on)
    {
        // Learning finished: switch to the learned profile once the audio thread has committed it.
        juce::Timer::callAfterDelay (200, [safe = juce::Component::SafePointer<DenoiseEditor> (this)] {
            if (safe != nullptr && safe->processor.denoiser.hasLearnedProfile())
                if (auto* param = safe->processor.params.getParameter ("mode"))
                    param->setValueNotifyingHost (1.0f);
        });
    }
}

void DenoiseEditor::timerCallback()
{
    juce::String status;
    if (processor.learning)
        status = "Learning... play a noise-only part, then press LEARN again";
    else if (processor.denoiser.hasLearnedProfile())
        status = modeBox.getSelectedItemIndex() == 1 ? "Using learned noise profile" : "Learned profile stored (select Learned to use it)";
    else
        status = modeBox.getSelectedItemIndex() == 1 ? "No profile yet: press LEARN on a noise-only part (Auto until then)"
                                                     : "Auto: tracking the noise floor of the last 8 seconds";
    profileStatus.setText (status, juce::dontSendNotification);
}

void DenoiseEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MajorCircle Denoise", 20, 12, 300, 26, juce::Justification::left);
    g.setColour (subtle);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v2  |  spectral noise reduction", 20, 36, 300, 16, juce::Justification::left);

    g.setColour (panel);
    g.fillRoundedRectangle (bandArea.toFloat(), 8.0f);
    g.fillRoundedRectangle (knobArea.toFloat(), 8.0f);

    g.setColour (subtle);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("BAND REDUCTION (% of Reduction)", bandArea.getX() + 12, bandArea.getY() + 6, 300, 14, juce::Justification::left);

    // Each band's track (the slider fills it) and value.
    g.setFont (juce::FontOptions (11.0f));
    for (auto& k : bandSliders)
    {
        g.setColour (grid);
        g.fillRoundedRectangle (k.slider.getBounds().toFloat(), 3.0f);
        g.setColour (subtle);
        g.drawText (juce::String ((int) k.slider.getValue()) + "%",
                    k.slider.getX() - 10, k.slider.getBottom() + 2, k.slider.getWidth() + 20, 14, juce::Justification::centred);
    }
}

void DenoiseEditor::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto header = area.removeFromTop (48);
    auto controls = header.removeFromRight (520).withTrimmedTop (6).withHeight (28);
    listenButton.setBounds (controls.removeFromRight (190));
    controls.removeFromRight (12);
    learnButton.setBounds (controls.removeFromRight (90));
    controls.removeFromRight (8);
    modeBox.setBounds (controls.removeFromRight (110));
    profileStatus.setBounds (getWidth() - 16 - 520, 50, 520, 16);
    profileStatus.setJustificationType (juce::Justification::right);

    area.removeFromTop (14);
    spectrumArea = area.removeFromTop (250);
    spectrum.setBounds (spectrumArea);

    area.removeFromTop (10);
    bandArea = area.removeFromTop (150);

    // Band sliders sit under their centre frequency on the spectrum.
    const int sliderWidth = 40;
    for (int b = 0; b < mcd::NUM_BANDS; ++b)
    {
        const float x = (float) spectrumArea.getX() + SpectrumView::freqToX (mcd::BAND_FREQS[b], (float) spectrumArea.getWidth());
        const int left = juce::jlimit (bandArea.getX() + 4, bandArea.getRight() - sliderWidth - 4, (int) x - sliderWidth / 2);
        auto& k = bandSliders[(size_t) b];
        k.label.setBounds (left - 10, bandArea.getY() + 22, sliderWidth + 20, 16);
        k.slider.setBounds (left, bandArea.getY() + 40, sliderWidth, 88);
    }

    area.removeFromTop (10);
    knobArea = area;
    auto knobRow = knobArea.reduced (12, 6);
    const int knobWidth = knobRow.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (knobWidth);
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell.withSizeKeepingCentre (110, cell.getHeight()));
    }
}
