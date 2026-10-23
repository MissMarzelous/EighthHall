// =============================================================================
//  PluginEditor.h - the plugin window.
//
//  Everything is laid out on a fixed 960 x 560 "design canvas" and then scaled
//  to whatever size the user drags the window to. That keeps the layout code
//  simple and makes the GUI look identical at every size (and sharp on
//  high-DPI / Retina screens).
// =============================================================================
#pragma once

#include "PluginProcessor.h"
#include "gui/DecayDisplay.h"
#include "gui/EighthHallLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

class EighthHallAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EighthHallAudioProcessorEditor (EighthHallAudioProcessor&);
    ~EighthHallAudioProcessorEditor() override;

    void resized() override;

    static constexpr int designWidth  = 960;
    static constexpr int designHeight = 560;

private:
    // One knob = name label + rotary slider + link to its parameter.
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label  name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // A labelled group of knobs ("SPACE", "TONE", ...).
    struct Group
    {
        juce::String title;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    // All visible content lives inside this component, which gets scaled.
    class Canvas final : public juce::Component
    {
    public:
        explicit Canvas (EighthHallAudioProcessorEditor& e) : editor (e) {}
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        EighthHallAudioProcessorEditor& editor;
    };

    void setUpKnob (Knob&, const char* paramId, const juce::String& label, bool bipolar = false);
    void timerCallback() override;

    EighthHallAudioProcessor& processor;
    eighthhall::gui::EighthHallLookAndFeel lookAndFeel;
    Canvas canvas { *this };

    eighthhall::gui::DecayDisplay decayDisplay;
    eighthhall::gui::LevelMeter   meter;

    Knob size, decay, preDelay, diffusion;
    Knob damping, lowCut, highCut;
    Knob modRate, modDepth;
    Knob width, mix;
    std::vector<Group> groups;

    juce::ComboBox presetBox;
    juce::ToggleButton freezeButton { "FREEZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EighthHallAudioProcessorEditor)
};
