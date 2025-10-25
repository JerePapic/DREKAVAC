#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Constructor
DREKAVACAudioProcessorEditor::DREKAVACAudioProcessorEditor(DREKAVACAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // --- Global LookAndFeel (triangles + font) ---
    customLAF = std::make_unique<CustomLookAndFeel>();
    outlinedButtonLAF = std::make_unique<OutlinedButtonLAF>();
    setLookAndFeel(customLAF.get());
    
    // Load background image
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::Background_png, BinaryData::Background_pngSize);

    // --- Sliders setup ---
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label,
                              const juce::String& paramID, const juce::String& labelText,
                              std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
    {
        slider.setLookAndFeel(customLAF.get());
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        addAndMakeVisible(slider);

        label.setText(labelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font("Gajraj One", 18.0f, juce::Font::plain));
        addAndMakeVisible(label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, paramID, slider);
    };

    setupSlider(driveSlider, driveLabel, "drive", "Drive", driveAttachment);
    setupSlider(toneSlider, toneLabel, "tone", "Tone", toneAttachment);
    setupSlider(distortionSlider, distortionLabel, "distortion", "Distortion", distortionAttachment);
    setupSlider(cutoffSlider, cutoffLabel, "cutoff", "Cutoff", cutoffAttachment);
    setupSlider(foldSlider, foldLabel, "fold", "Fold", foldAttachment);
    setupSlider(flavorSlider, flavorLabel, "flavor", "Flavor", flavorAttachment);
    setupSlider(outputSlider, outputLabel, "output", "Output", outputAttachment);
    setupSlider(drywetSlider, drywetLabel, "drywet", "Dry/Wet", drywetAttachment);

    // Collect sliders for easier layout
    sliders = {
        { &driveSlider, &driveLabel },
        { &toneSlider, &toneLabel },
        { &distortionSlider, &distortionLabel },
        { &cutoffSlider, &cutoffLabel },
        { &foldSlider, &foldLabel },
        { &flavorSlider, &flavorLabel },
        { &outputSlider, &outputLabel },
        { &drywetSlider, &drywetLabel }
    };

    // --- Preset labels ---
    presetTitleLabel.setText("PRESET", juce::dontSendNotification);
    presetTitleLabel.setJustificationType(juce::Justification::centred);
    presetTitleLabel.setFont(juce::Font("Gajraj One", 16.0f, juce::Font::plain));
    presetTitleLabel.setColour(juce::Label::textColourId, juce::Colour(232, 232, 232));
    addAndMakeVisible(presetTitleLabel);

    presetNameLabel.setText("Default", juce::dontSendNotification);
    presetNameLabel.setJustificationType(juce::Justification::centred);
    presetNameLabel.setFont(juce::Font("Gajraj One", 14.0f, juce::Font::plain));
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(205, 70, 130));
    addAndMakeVisible(presetNameLabel);

    // --- Save/Load buttons ---
    outlinedButtonLAF = std::make_unique<OutlinedButtonLAF>();

    saveButton.setButtonText("SAVE");
    saveButton.setLookAndFeel(outlinedButtonLAF.get());
    addAndMakeVisible(saveButton);

    loadButton.setButtonText("LOAD");
    loadButton.setLookAndFeel(outlinedButtonLAF.get());
    addAndMakeVisible(loadButton);

// Save button
saveButton.onClick = [this]()
{
    // Create chooser directly inside launchAsync
    (new juce::FileChooser("Save Preset", {}, "*.preset"))
        ->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.getFullPathName().isNotEmpty())
                {
                    audioProcessor.savePresetToFile(file);
                    presetNameLabel.setText(file.getFileNameWithoutExtension(), juce::dontSendNotification);
                }
            });
};

// Load button
loadButton.onClick = [this]()
{
    (new juce::FileChooser("Load Preset", {}, "*.preset"))
        ->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    audioProcessor.loadPresetFromFile(file);
                    presetNameLabel.setText(file.getFileNameWithoutExtension(), juce::dontSendNotification);
                }
            });
};

    setSize(400, 600);
}

DREKAVACAudioProcessorEditor::~DREKAVACAudioProcessorEditor()
{
    saveButton.setLookAndFeel(nullptr);
    loadButton.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

//==============================================================================

void DREKAVACAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(20, 20, 30));
    
    // Draw background image if available
    if (backgroundImage.isValid())
        g.drawImageAt(backgroundImage, 0, 60); // just place it 60px down
    else
        g.fillAll(juce::Colour(18, 18, 25));
        
    // Border + rectangles
    g.setColour(juce::Colour(61, 57, 97));
    g.fillRect(0, 0, getWidth(), 60);
    
    g.fillRect(0, getHeight() - 60, getWidth(), 60);
    g.setColour(juce::Colour(205, 70, 130));
    g.fillRect(0, 59, getWidth(), 2);                          // line below header
    g.fillRect(0, getHeight() - 61, getWidth(), 2);            // line above footer
    

    g.drawRect(getLocalBounds(), 2.0f);

    // --- Main title ---
    g.setFont(juce::Font("Gajraj One", 48.0f, juce::Font::plain));
    g.drawText("DREKAVAC", 0, 0, getWidth(), 60, juce::Justification::centred);

    // --- Subtitle ---
    g.setColour(juce::Colour(232, 232, 232));
    g.setFont(juce::Font("Gajraj One", 24.0f, juce::Font::plain));
    g.drawText("DISTEK", 0, 40, getWidth(), 20, juce::Justification::centred);
}

void DREKAVACAudioProcessorEditor::resized()
{
    int margin = 20;
    int sliderHeight = 40;
    int labelHeight = 20;
    int spacing = 15;

    int columnWidth = (getWidth() - 3 * margin) / 2;
    int yLeft = 150, yRight = 150;

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        auto& s = sliders[i];
        int x = (i % 2 == 0) ? margin : margin * 2 + columnWidth;
        int& y = (i % 2 == 0) ? yLeft : yRight;

        s.label->setBounds(x, y, columnWidth, labelHeight);
        y += labelHeight;

        s.slider->setBounds(x, y, columnWidth, sliderHeight);
        y += sliderHeight + spacing;
    }

    // --- Footer area ---
    auto footerTop = getHeight() - 60;

    presetTitleLabel.setBounds(10, footerTop + 5, 100, 20);
    presetNameLabel.setBounds(10, footerTop + 25, 100, 30);

    int buttonWidth = 100;
    int buttonHeight = 22;
    int buttonGap = 5;
    int rightX = getWidth() - buttonWidth - 10;

    saveButton.setBounds(rightX, footerTop + 5, buttonWidth, buttonHeight);
    loadButton.setBounds(rightX, footerTop + 5 + buttonHeight + buttonGap, buttonWidth, buttonHeight);
}
