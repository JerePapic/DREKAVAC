#include "PluginProcessor.h"
#include "PluginEditor.h" // keep this include if your editor header uses the processor type

// Helper to create parameter layout
juce::AudioProcessorValueTreeState::ParameterLayout DREKAVACAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>("drive", "Drive", 0.0f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
    "tone", "Tone",
    juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
    juce::String(), juce::AudioProcessorParameter::genericParameter,
    [](float value, int) {
        return juce::String((int)(value * 100.0f)) + "%"; // display as 0–100%
    },
    [](const juce::String& text) {
        return text.upToFirstOccurrenceOf("%", false, false).getFloatValue() / 100.0f;
    }
));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("distortion", "Distortion", 0.0f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
    "cutoff", "Cutoff",
    juce::NormalisableRange<float>(0.0f, 1.0f), 0.75f,
    juce::String(), juce::AudioProcessorParameter::genericParameter,
    // Display function (shows Hz)
    [](float value, int) {
        const float minHz = 100.0f, maxHz = 8000.0f;
        const float exponent = 0.7f;
        float hz = minHz * std::pow(maxHz / minHz, std::pow(value, exponent));
        return juce::String((int)hz) + " Hz";
    },
    // Text-to-value (parsing typed-in Hz values)
    [](const juce::String& text) {
        const float minHz = 100.0f, maxHz = 8000.0f;
        const float exponent = 0.7f;
        float hz = text.upToFirstOccurrenceOf("Hz", false, false).getFloatValue();
        float norm = std::pow(std::log(hz / minHz) / std::log(maxHz / minHz), 1.0f / exponent);
        return juce::jlimit(0.0f, 1.0f, norm);
    }
));

    
params.push_back(std::make_unique<juce::AudioParameterFloat>(
    "fold", "Fold",
    juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f,
    juce::String(), juce::AudioProcessorParameter::genericParameter,
    [](float value, int){
        return juce::String((int)(value * 100.0f)) + "%"; 
    },
    [](const juce::String& text){
        return text.upToFirstOccurrenceOf("%", false, false).getFloatValue() / 100.0f; }
));



    // Flavor parameter (internal 0–1, displayed as -100% → +100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "flavor", "Flavor",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            int percent = static_cast<int>((value * 200.0f) - 100.0f);
            return juce::String(percent) + "%";
        },
        [](const juce::String& text) {
            auto t = text.upToFirstOccurrenceOf("%", false, false).getFloatValue();
            return (t + 100.0f) / 200.0f;
        }
    ));

    // Output gain (0–2, displayed as 0%–200%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output", "Output",
        juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            return juce::String((int)(value * 100.0f)) + "%";
        },
        [](const juce::String& text) {
            auto t = text.upToFirstOccurrenceOf("%", false, false).getFloatValue();
            return t / 100.0f;
        }
    ));

    // Dry/Wet mix (0–1, displayed as 0%–100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "drywet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            return juce::String((int)(value * 100.0f)) + "%";
        },
        [](const juce::String& text) {
            auto t = text.upToFirstOccurrenceOf("%", false, false).getFloatValue();
            return t / 100.0f;
        }
    ));

    return { params.begin(), params.end() };
}

//==============================================================================

DREKAVACAudioProcessor::DREKAVACAudioProcessor()
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
#endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
                      ),
      parameters(*this, nullptr, juce::Identifier("DREKAVAC_PARAMETERS"), createParameterLayout())
{
}

DREKAVACAudioProcessor::~DREKAVACAudioProcessor() {}

//==============================================================================

const juce::String DREKAVACAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

void DREKAVACAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    // Reset oversampler and prepare it
    oversampler.reset();
    oversampler.initProcessing(samplesPerBlock);

    // Prepare ToneProcessor
    toneProcessor.prepare(sampleRate);
    dist.prepare(getSampleRate());

    // Reset other DSP modules
    overdrive.setDrive(1.0f); // default values
    overdrive.setTone(0.5f);

    dist.setPreGain(1.0f);
    dist.setCutoffSliderValue(0.5f);

    fold.setDepth(0.0f);
    simpleComp.prepare(getSampleRate());

}


void DREKAVACAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DREKAVACAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

 #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
 #endif

    return true;
#endif
}
#endif

//==============================================================================

void DREKAVACAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // --- Upsample ---
    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto oversampledBlock = oversampler.processSamplesUp(block);

    // --- Get parameter values ---
    float drive      = *parameters.getRawParameterValue("drive");
    float tone       = *parameters.getRawParameterValue("tone");
    float distortion = *parameters.getRawParameterValue("distortion");
    float cutoff  = *parameters.getRawParameterValue("cutoff");   // 0..1 slider
    float foldDepth  = *parameters.getRawParameterValue("fold");
    float flavor     = *parameters.getRawParameterValue("flavor");
    float outputGain = *parameters.getRawParameterValue("output");
    float drywet     = *parameters.getRawParameterValue("drywet");

    // --- Update DSP modules ---
    overdrive.setDrive(drive);
    overdrive.setTone(tone);
    toneProcessor.setParameters(tone, drive);
    dist.setPreGain(std::max(0.0f, distortion));
    dist.setCutoffSliderValue(cutoff); // <-- NEW LOGARITHMIC MAPPING
    fold.setDepth(foldDepth);

    float oversampledRate = static_cast<float>(getSampleRate()) * oversampler.getOversamplingFactor();
    const float preGain = 0.6f;

    int numSamples = oversampledBlock.getNumSamples();

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = oversampledBlock.getChannelPointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float inputSample = channelData[sample];
            float scaledInput = inputSample * preGain;

            float odSample   = overdrive.processSample(scaledInput, oversampledRate);
            float distSample = dist.processSample(odSample);
            float foldSample = fold.processSample(odSample);

            float f = std::sin(flavor * juce::MathConstants<float>::halfPi);
            float parallel = odSample + distSample * (1.0f - f) + foldSample * f;

            // --- Tone filtering ---
            float filtered = toneProcessor.processSample(parallel);

            float w = std::sqrt(drywet);
            float mixed = inputSample * (1.0f - w) + filtered * w;

            mixed *= outputGain;
            mixed = std::tanh(mixed);

            channelData[sample] = mixed;
        }
    }

    // --- Downsample ---
    oversampler.processSamplesDown(block);
}



//==============================================================================

void DREKAVACAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DREKAVACAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// Simple preset functions (editor can call these)
void DREKAVACAudioProcessor::savePresetToFile(const juce::File& file)
{
    if (!file.hasFileExtension(".preset"))
        return;

    // Create an XML representation of all parameters
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute("presetName", currentPresetName);
        xml->writeTo(file);
    }
}

void DREKAVACAudioProcessor::loadPresetFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    std::unique_ptr<juce::XmlElement> xml = juce::XmlDocument::parse(file);

    if (xml && xml->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
        currentPresetName = xml->getStringAttribute("presetName", "Unknown");
    }
}

void DREKAVACAudioProcessor::notifyUIUpdate()
{
    // This makes the host & GUI aware of parameter changes
    if (auto* editor = dynamic_cast<DREKAVACAudioProcessorEditor*>(getActiveEditor()))
        editor->repaint();

    updateHostDisplay();
}

//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DREKAVACAudioProcessor();
}

bool DREKAVACAudioProcessor::hasEditor() const
{
    return true; // plugin has an editor
}

juce::AudioProcessorEditor* DREKAVACAudioProcessor::createEditor()
{
    return new DREKAVACAudioProcessorEditor(*this);
}