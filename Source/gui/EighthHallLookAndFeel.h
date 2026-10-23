// =============================================================================
//  EighthHallLookAndFeel.h - the plugin's visual style.
//
//  In JUCE, a LookAndFeel class decides how standard widgets (knobs, buttons,
//  menus) are drawn. Overriding a few draw functions gives the whole plugin a
//  custom, branded look without writing new widget classes.
// =============================================================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace eighthhall::gui
{
namespace colours
{
    inline const juce::Colour background { 0xff0e1015 };
    inline const juce::Colour panel      { 0xff161a22 };
    inline const juce::Colour panelEdge  { 0xff232936 };
    inline const juce::Colour track      { 0xff2a303d };
    inline const juce::Colour accent     { 0xff5cc8dc };   // kingfisher blue
    inline const juce::Colour accentWarm { 0xfff0b46a };   // used for "highs"
    inline const juce::Colour text       { 0xffe6e8ee };
    inline const juce::Colour textDim    { 0xff8a91a3 };
}

class EighthHallLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    EighthHallLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};
} // namespace eighthhall::gui
