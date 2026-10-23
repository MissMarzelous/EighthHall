#include "EighthHallLookAndFeel.h"

namespace eighthhall::gui
{
using namespace juce;

EighthHallLookAndFeel::EighthHallLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (Label::textColourId, colours::text);

    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.35f));

    setColour (ComboBox::backgroundColourId, colours::panel);
    setColour (ComboBox::outlineColourId, colours::panelEdge);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::arrowColourId, colours::accent);

    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, colours::text);

    setColour (TextEditor::backgroundColourId, colours::panel);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    setColour (CaretComponent::caretColourId, colours::accent);
}

void EighthHallLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float startAngle, float endAngle, Slider& slider)
{
    const auto bounds  = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre  = bounds.getCentre();
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const float lineW  = jmax (2.5f, radius * 0.11f);
    const float arcR   = radius - lineW * 0.5f;
    const bool  hover  = slider.isMouseOverOrDragging();

    // Background track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // Value arc (bipolar-looking for Width, which is centred at 100%)
    const bool centred = slider.getProperties().getWithDefault ("bipolar", false);
    const float from = centred ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
    Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (colours::accent.withAlpha (hover ? 1.0f : 0.9f));
    g.strokePath (value, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // Knob body
    const float bodyR = arcR - lineW * 1.4f;
    ColourGradient body (colours::panel.brighter (0.25f), centre.x, centre.y - bodyR,
                         colours::panel.darker (0.4f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (body);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (colours::panelEdge.brighter (hover ? 0.3f : 0.0f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer
    const auto tip   = centre.getPointOnCircumference (bodyR * 0.78f, angle);
    const auto inner = centre.getPointOnCircumference (bodyR * 0.30f, angle);
    g.setColour (colours::text);
    g.drawLine ({ inner, tip }, jmax (2.0f, lineW * 0.6f));
}

void EighthHallLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool down)
{
    const auto r  = b.getLocalBounds().toFloat().reduced (4.0f);
    const bool on = b.getToggleState();
    const float corner = r.getHeight() * 0.5f;

    if (on)
    {
        // Soft glow around the button when frozen
        for (int i = 1; i <= 3; ++i)
        {
            const float grow = (float) i * 1.2f;
            g.setColour (colours::accent.withAlpha (0.10f / (float) i));
            g.fillRoundedRectangle (r.expanded (grow), corner + grow);
        }
        g.setColour (colours::accent.withAlpha (down ? 0.55f : 0.35f));
    }
    else
    {
        g.setColour (colours::panel.brighter (highlighted ? 0.12f : 0.0f));
    }
    g.fillRoundedRectangle (r, corner);

    g.setColour (on ? colours::accent : colours::panelEdge.brighter (highlighted ? 0.4f : 0.15f));
    g.drawRoundedRectangle (r, corner, 1.2f);

    // Snowflake-ish dot + label
    const float dotR = r.getHeight() * 0.16f;
    const auto dotCentre = Point<float> (r.getX() + corner, r.getCentreY());
    g.setColour (on ? colours::accent : colours::textDim);
    g.fillEllipse (dotCentre.x - dotR, dotCentre.y - dotR, dotR * 2.0f, dotR * 2.0f);

    g.setColour (on ? colours::text : colours::textDim);
    g.setFont (FontOptions (r.getHeight() * 0.42f, Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (corner * 1.4f), Justification::centred);
}

void EighthHallLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool,
                                       int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId).brighter (box.isMouseOver() ? 0.08f : 0.0f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // Down arrow
    const float s = (float) height * 0.18f;
    const float cx = (float) width - (float) height * 0.5f;
    const float cy = (float) height * 0.5f;
    Path arrow;
    arrow.startNewSubPath (cx - s, cy - s * 0.5f);
    arrow.lineTo (cx, cy + s * 0.5f);
    arrow.lineTo (cx + s, cy - s * 0.5f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.strokePath (arrow, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font EighthHallLookAndFeel::getComboBoxFont (ComboBox& box)
{
    return FontOptions (jmin (15.0f, (float) box.getHeight() * 0.5f));
}

Font EighthHallLookAndFeel::getPopupMenuFont()
{
    return FontOptions (15.0f);
}

Font EighthHallLookAndFeel::getLabelFont (Label& label)
{
    return label.getFont();
}

Label* EighthHallLookAndFeel::createSliderTextBox (Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (FontOptions (13.0f));
    l->setJustificationType (Justification::centred);
    return l;
}
} // namespace eighthhall::gui
