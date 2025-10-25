#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Custom LookAndFeel for outlined buttons
struct OutlinedButtonLAF : public juce::LookAndFeel_V4
{
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& /*backgroundColour*/,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        juce::Colour baseColour(61, 57, 97);

        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.3f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.darker(0.15f);

        g.setColour(baseColour);
        g.fillRect(bounds);

        g.setColour(juce::Colour(205, 70, 130));
        g.drawRect(bounds, 2.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool /*shouldDrawButtonAsHighlighted*/,
                        bool /*shouldDrawButtonAsDown*/) override
    {
        g.setColour(juce::Colour(205, 70, 130));
        g.setFont(juce::Font("Gajraj One", 18.0f, juce::Font::plain));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds(),
                         juce::Justification::centred, 1);
    }
};

//==============================================================================
// Custom LookAndFeel for plugin
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel()
    {
        gajrajTypeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::GajrajOneRegular_ttf,
            BinaryData::GajrajOneRegular_ttfSize
        );
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override
    {
        return gajrajTypeface;
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(juce::Font("Gajraj One", 24.0f, juce::Font::plain));
        g.drawFittedText(label.getText(), label.getLocalBounds(),
                         label.getJustificationType(), 1);
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                      float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                      const juce::Slider::SliderStyle style, juce::Slider& /*slider*/) override
{
    // Draw track
    g.setColour(juce::Colour(80, 80, 80)); // subtle grey for track
    if (style == juce::Slider::LinearHorizontal)
        g.fillRect(x, height / 2 - 2, width, 4);
    else
        g.fillRect(width / 2 - 2, y, 4, height);

    // Triangle handle
    juce::Path triangle;
    const float handleWidth  = 16.0f;
    const float handleHeight = 16.0f;
    const float centerY      = height / 2.0f;

    triangle.startNewSubPath(sliderPos, centerY + handleHeight / 2.0f);
    triangle.lineTo(sliderPos - handleWidth / 2.0f, centerY - handleHeight / 2.0f);
    triangle.lineTo(sliderPos + handleWidth / 2.0f, centerY - handleHeight / 2.0f);
    triangle.closeSubPath();

    // Fill color (DREKAVAC title color)
    g.setColour(juce::Colour(205, 70, 130));
    g.fillPath(triangle);

    // Outline color (white text color)
    g.setColour(juce::Colour(232, 232, 232));
    g.strokePath(triangle, juce::PathStrokeType(2.0f));
}
private:
    juce::Typeface::Ptr gajrajTypeface;
};

//==============================================================================
// Editor
class DREKAVACAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    DREKAVACAudioProcessorEditor(DREKAVACAudioProcessor&);
    ~DREKAVACAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DREKAVACAudioProcessor& audioProcessor;

    // LookAndFeel
    std::unique_ptr<CustomLookAndFeel> customLAF;
    std::unique_ptr<OutlinedButtonLAF> outlinedButtonLAF;

    // Background
    juce::Image backgroundImage;

    // Sliders
    juce::Slider driveSlider, toneSlider, distortionSlider, cutoffSlider;
    juce::Slider foldSlider, flavorSlider, outputSlider, drywetSlider;

    // Labels
    juce::Label driveLabel, toneLabel, distortionLabel, cutoffLabel;
    juce::Label foldLabel, flavorLabel, outputLabel, drywetLabel;
    juce::Label presetTitleLabel, presetNameLabel;

    // Buttons
    juce::TextButton saveButton;
    juce::TextButton loadButton;

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> toneAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> distortionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> foldAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> flavorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> drywetAttachment;

    struct SliderWithLabel
    {
        juce::Slider* slider;
        juce::Label* label;
    };
    std::vector<SliderWithLabel> sliders;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DREKAVACAudioProcessorEditor)
};
