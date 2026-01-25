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
        [](float value, int) {
            return juce::String((int)(value * 100.0f)) + "%";
        },
        [](const juce::String& text) {
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
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    parameters(*this, nullptr, juce::Identifier("DREKAVAC_PARAMETERS"), createParameterLayout()),
    oversampler(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true)  // <-- CHANGED: maximally flat phase
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
    // Initialize oversampler FIRST
    oversampler.reset();
    oversampler.initProcessing(samplesPerBlock);

    // Calculate oversampled rate
    double oversampledRate = sampleRate * oversampler.getOversamplingFactor();

    // Prepare DSP modules with correct sample rates
    toneProcessor.prepare(sampleRate); // ToneProcessor works at original rate
    dist.prepare(oversampledRate);     // Distortion works at oversampled rate
    simpleComp.prepare(sampleRate, getTotalNumInputChannels());    // Compressor works at original rate

    // Reset other DSP modules with default values
    overdrive.setDrive(1.0f);
    overdrive.setTone(0.5f);

    dist.setPreGain(1.0f);
    dist.setCutoffSliderValue(0.5f);

    fold.setDepth(0.0f);
    
    dcBlocker.prepare(sampleRate, getTotalNumInputChannels());
    simpleComp.prepare(sampleRate, getTotalNumInputChannels());
    
    auto smoothingTime = 0.02; // 20ms smoothing for all

    driveSmoothed.reset(sampleRate, smoothingTime);
    toneSmoothed.reset(sampleRate, smoothingTime);
    distortionSmoothed.reset(sampleRate, smoothingTime);
    cutoffSmoothed.reset(sampleRate, smoothingTime);
    foldSmoothed.reset(sampleRate, smoothingTime);
    flavorSmoothed.reset(sampleRate, smoothingTime);
    outputSmoothed.reset(sampleRate, smoothingTime);
    drywetSmoothed.reset(sampleRate, smoothingTime);

    // Initialize current value
    driveSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("drive"));
    toneSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("tone"));
    distortionSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("distortion"));
    cutoffSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("cutoff"));
    foldSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("fold"));
    flavorSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("flavor"));
    outputSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("output"));
    drywetSmoothed.setCurrentAndTargetValue(*parameters.getRawParameterValue("drywet"));

}

void DREKAVACAudioProcessor::releaseResources()
{
    oversampler.reset();
    dcBlocker.reset();
    simpleComp.prepare(getSampleRate(), getTotalNumInputChannels());
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DREKAVACAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
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
void DREKAVACAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    //==================================================
    // STEP 1: Update smoothed targets (once per block)
    //==================================================
    driveSmoothed.setTargetValue(*parameters.getRawParameterValue("drive"));
    toneSmoothed.setTargetValue(*parameters.getRawParameterValue("tone"));
    distortionSmoothed.setTargetValue(*parameters.getRawParameterValue("distortion"));
    cutoffSmoothed.setTargetValue(*parameters.getRawParameterValue("cutoff"));
    foldSmoothed.setTargetValue(*parameters.getRawParameterValue("fold"));
    flavorSmoothed.setTargetValue(*parameters.getRawParameterValue("flavor"));
    outputSmoothed.setTargetValue(*parameters.getRawParameterValue("output"));
    drywetSmoothed.setTargetValue(*parameters.getRawParameterValue("drywet"));

    //==================================================
    // STEP 2: CONTROL-RATE DSP UPDATES (ONCE PER BLOCK)
    //==================================================
    const float driveCtrl      = driveSmoothed.getTargetValue();
    const float toneCtrl       = toneSmoothed.getTargetValue();
    const float distortionCtrl = distortionSmoothed.getTargetValue();
    const float cutoffCtrl     = cutoffSmoothed.getTargetValue();
    const float foldCtrl       = foldSmoothed.getTargetValue();

    // Update all coefficients ONCE per block (not per sample!)
    overdrive.setDrive(driveCtrl);
    overdrive.setTone(toneCtrl);
    dist.setPreGain(std::max(0.0f, distortionCtrl));
    dist.setCutoffSliderValue(cutoffCtrl);  // This updates filter coefficients
    fold.setDepth(foldCtrl);
    toneProcessor.setParameters(toneCtrl, driveCtrl); // Add this if needed

    //==================================================
    // STEP 3: OVERSAMPLE
    //==================================================
    auto block = juce::dsp::AudioBlock<float>(buffer);
    auto osBlock = oversampler.processSamplesUp(block);

    const int numSamples = osBlock.getNumSamples();
    const double osRate  = getSampleRate() * oversampler.getOversamplingFactor();
    const float preGain  = 0.6f;

    //==================================================
    // STEP 4: AUDIO-RATE PROCESSING
    //==================================================
    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        float* channelData = osBlock.getChannelPointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = channelData[i];

            // Smoothed values SAFE per-sample
            const float flavor   = flavorSmoothed.getNextValue();
            const float output   = outputSmoothed.getNextValue();
            const float dryWet   = drywetSmoothed.getNextValue();

            const float flavorMix = std::sin(flavor * juce::MathConstants<float>::halfPi);
            const float wetMix    = std::sqrt(dryWet);

            // --- Distortion chain (oversampled) ---
            float x = input * preGain;
            float od   = overdrive.processSample(x, osRate);
            float distSample = dist.processSample(od);
            float foldS = fold.processSample(od);

            float parallel = od
                           + distSample  * (1.0f - flavorMix)
                           + foldS * flavorMix;

            // --- Downstream (still oversampled) ---
            float y = std::tanh(parallel);

            // --- Mix ---
            float mixed = input * (1.0f - wetMix) + y * wetMix;

            // --- DC blocker (per channel) ---
            mixed = dcBlocker.processSample(mixed, ch);

            // --- Output gain ---
            mixed *= output;

            // --- Soft safety clip ---
            mixed = std::tanh(mixed);

            // --- Compressor ---
            mixed = simpleComp.processSample(mixed, ch);

            channelData[i] = mixed;
        }
    }

    //==================================================
    // STEP 5: ANTI-ALIASING FILTER BEFORE DOWNSAMPLE
    //==================================================
    // Add a gentle lowpass before downsampling to prevent aliasing
    const float antiAliasCutoff = 18000.0f; // Hz (below Nyquist of original rate)
    const float antiAliasAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * 
                                         antiAliasCutoff / (float)osRate);
    
    for (int ch = 0; ch < totalNumInputChannels; ++ch)
    {
        float* channelData = osBlock.getChannelPointer(ch);
        static std::vector<float> prevSample(totalNumInputChannels, 0.0f);
        
        for (int i = 0; i < numSamples; ++i)
        {
            channelData[i] = prevSample[ch] + antiAliasAlpha * (channelData[i] - prevSample[ch]);
            prevSample[ch] = channelData[i];
        }
    }

    //==================================================
    // STEP 6: DOWNSAMPLE
    //==================================================
    oversampler.processSamplesDown(block);
}




//==============================================================================

void DREKAVACAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DREKAVACAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr)
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
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
