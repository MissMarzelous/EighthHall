#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"

using namespace eighthhall;
namespace colours = eighthhall::gui::colours;

EighthHallAudioProcessorEditor::EighthHallAudioProcessorEditor (EighthHallAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      decayDisplay (p.state),
      meter (p.outputPeak)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (decayDisplay);
    canvas.addAndMakeVisible (meter);

    namespace id = params::id;
    setUpKnob (size,      id::size,      "Size");
    setUpKnob (decay,     id::decay,     "Decay");
    setUpKnob (preDelay,  id::preDelay,  "Pre-Delay");
    setUpKnob (diffusion, id::diffusion, "Diffusion");
    setUpKnob (damping,   id::damping,   "Damping");
    setUpKnob (lowCut,    id::lowCut,    "Low Cut");
    setUpKnob (highCut,   id::highCut,   "High Cut");
    setUpKnob (modRate,   id::modRate,   "Rate");
    setUpKnob (modDepth,  id::modDepth,  "Depth");
    setUpKnob (width,     id::width,     "Width", true);
    setUpKnob (mix,       id::mix,       "Mix");

    groups = { { "SPACE",  { &size, &decay, &preDelay, &diffusion }, {} },
               { "TONE",   { &damping, &lowCut, &highCut }, {} },
               { "MOTION", { &modRate, &modDepth }, {} },
               { "OUTPUT", { &width, &mix }, {} } };

    // Presets
    const auto& presets = getFactoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        presetBox.addItem (presets[(size_t) i].name, i + 1);
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        if (const int id = presetBox.getSelectedId(); id > 0)
            processor.setCurrentProgram (id - 1);
    };
    presetBox.setTooltip ("Factory presets");
    canvas.addAndMakeVisible (presetBox);

    // Freeze
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.state, id::freeze, freezeButton);
    freezeButton.setClickingTogglesState (true);
    canvas.addAndMakeVisible (freezeButton);

    // Window sizing: resizable, locked to the design's aspect ratio.
    setResizable (true, true);
    setResizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth, designHeight);

    startTimerHz (10);
}

EighthHallAudioProcessorEditor::~EighthHallAudioProcessorEditor()
{
    // Widgets must stop using the LookAndFeel before it is destroyed.
    setLookAndFeel (nullptr);
}

void EighthHallAudioProcessorEditor::setUpKnob (Knob& k, const char* paramId, const juce::String& label, bool bipolar)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    // Set explicitly: the slider was created before our LookAndFeel was applied.
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, colours::text);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.getProperties().set ("bipolar", bipolar);

    if (auto* param = processor.state.getParameter (paramId))
    {
        // Double-click a knob to return it to its default.
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        k.slider.setTooltip (param->getName (64));
    }

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.state, paramId, k.slider);

    k.name.setText (label.toUpperCase(), juce::dontSendNotification);
    k.name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.name.setColour (juce::Label::textColourId, colours::textDim);
    k.name.setJustificationType (juce::Justification::centred);
    k.name.setInterceptsMouseClicks (false, false);

    canvas.addAndMakeVisible (k.slider);
    canvas.addAndMakeVisible (k.name);
}

void EighthHallAudioProcessorEditor::timerCallback()
{
    // Keep the preset box in sync if the host changes the program.
    const int id = processor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void EighthHallAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));
    canvas.setBounds (0, 0, designWidth, designHeight);
}

// =============================================================================
//  Canvas: layout + background drawing in design coordinates (960 x 560)
// =============================================================================
void EighthHallAudioProcessorEditor::Canvas::resized()
{
    auto& e = editor;
    auto area = getLocalBounds().reduced (20, 0);

    // Header
    auto header = area.removeFromTop (68);
    e.freezeButton.setBounds (header.removeFromRight (128).withSizeKeepingCentre (128, 40));
    header.removeFromRight (14);
    e.presetBox.setBounds (header.removeFromRight (230).withSizeKeepingCentre (230, 32));

    // Display + meter
    auto displayRow = area.removeFromTop (246);
    e.meter.setBounds (displayRow.removeFromRight (58));
    displayRow.removeFromRight (12);
    e.decayDisplay.setBounds (displayRow);

    area.removeFromTop (14);
    area.removeFromBottom (30);   // footer

    // Groups: widths proportional to how many knobs each holds
    constexpr int gap = 12;
    int totalKnobs = 0;
    for (auto& g : e.groups)
        totalKnobs += (int) g.knobs.size();

    const int usable = area.getWidth() - gap * ((int) e.groups.size() - 1);
    for (auto& g : e.groups)
    {
        const int w = usable * (int) g.knobs.size() / totalKnobs;
        g.bounds = area.removeFromLeft (w);
        area.removeFromLeft (gap);

        auto inner = g.bounds.reduced (6, 0);
        inner.removeFromTop (30);   // group title
        inner.removeFromBottom (8);
        const int cellW = inner.getWidth() / (int) g.knobs.size();

        // Each knob is a vertical stack (name / dial / value), as big as the
        // cell allows and centred in it.
        constexpr int nameH = 18, valueH = 20;
        const int dial  = juce::jmin (cellW - 6, inner.getHeight() - nameH - valueH - 6);
        const int stack = nameH + dial + valueH + 6;

        for (auto* k : g.knobs)
        {
            auto cell = inner.removeFromLeft (cellW).withSizeKeepingCentre (cellW, stack);
            k->name.setBounds (cell.removeFromTop (nameH));
            cell.removeFromTop (6);
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (cellW - 8, 84), valueH);
            k->slider.setBounds (cell.withSizeKeepingCentre (juce::jmax (dial, juce::jmin (cellW - 4, 88)), cell.getHeight()));
        }
    }
}

void EighthHallAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    using namespace juce;

    // Background with a subtle vertical gradient
    g.setGradientFill (ColourGradient (colours::background.brighter (0.06f), 0.0f, 0.0f,
                                       colours::background, 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Title
    auto header = getLocalBounds().reduced (20, 0).removeFromTop (68);
    g.setColour (colours::text);
    g.setFont (FontOptions (30.0f, Font::bold).withKerningFactor (0.12f));
    g.drawText ("EIGHTH HALL", header.removeFromLeft (262), Justification::centredLeft);

    g.setColour (colours::accent);
    g.setFont (FontOptions (12.0f, Font::bold).withKerningFactor (0.2f));
    g.drawText ("ALGORITHMIC REVERB", header.removeFromLeft (190).translated (0, 4), Justification::centredLeft);

    // Group panels
    for (auto& grp : editor.groups)
    {
        const auto r = grp.bounds.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (colours::accent);
        g.setFont (FontOptions (12.0f, Font::bold).withKerningFactor (0.25f));
        g.drawText (grp.title, r.withHeight (30.0f).reduced (14.0f, 0.0f), Justification::centredLeft);
    }

    // Footer
    auto footer = getLocalBounds().reduced (22, 0).removeFromBottom (28);
    g.setColour (colours::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText ("Fabrick Audio  -  v" JucePlugin_VersionString, footer, Justification::centredLeft);
    g.drawText ("Double-click a knob to reset  -  drag window corner to resize", footer, Justification::centredRight);
}
