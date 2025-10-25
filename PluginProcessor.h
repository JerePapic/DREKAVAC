#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Simple DSP helper classes (kept small and copy-paste friendly)

//==============================================================================

class ToneProcessor
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;

        // Initialize filter coefficients
        updateCoefficients();
        
        lowFilter.reset();
        highFilter.reset();
    }

    void setParameters(float tone, float drive)
    {
        // tone modulation
        float modulated = tone - 0.15f * (drive / 10.0f);
        balance = juce::jlimit(0.0f, 1.0f, tone - 0.15f * (drive/10.0f));


        updateCoefficients();
    }

    float processSample(float input)
    {
        float low  = lowFilter.processSample(input);
        float high = highFilter.processSample(input);
        return juce::jmap(balance, low, high);
    }

    void reset()
    {
        lowFilter.reset();
        highFilter.reset();
    }

private:
    juce::dsp::IIR::Filter<float> lowFilter;
    juce::dsp::IIR::Filter<float> highFilter;

    double fs = 44100.0;
    float balance = 0.5f;

    const float pivotFreq = 1000.0f;
    const float q = 0.707f;

    void updateCoefficients()
    {
        // Create coefficient shared pointers
        auto lowShelf  = juce::dsp::IIR::Coefficients<float>::makeLowShelf(fs, pivotFreq, q, 1.0f + (1.0f - balance) * 1.5f);
        auto highShelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf(fs, pivotFreq, q, 1.0f + balance * 1.5f);

        // Assign to filters via setCoefficients
        lowFilter.coefficients = lowShelf;
        highFilter.coefficients = highShelf;
    }
};

class Overdrive
{
public:
    Overdrive() : drive(1.0f), tone(0.5f), prevY(0.0f) {}

    void setDrive(float d)  { drive = d; }
    void setTone (float t)  { tone = juce::jlimit(0.0f, 1.0f, t); }

    float processSample(float input, double sampleRate)
    {
        // input gain (gentle curve)
        float x = input * (1.0f + std::pow(drive, 2.0f));

        // soft clipping
        float y = std::tanh(x);

        // simple one-pole lowpass for 'tone' (tone=0 darker, tone=1 brighter)
        float cutoff = 200.0f + tone * 8000.0f; // 200..8200 Hz
        float RC = 1.0f / (2.0f * juce::MathConstants<float>::pi * cutoff);
        float dt = 1.0f / (float)sampleRate;
        float alpha = dt / (RC + dt);

        prevY = prevY + alpha * (y - prevY);
        return prevY;
    }

private:
    float drive, tone;
    float prevY;
};

class Distortion
{
public:
    Distortion() : preGain(1.0f), cutoff(12000.0f), fs(44100.0) {}

    void setPreGain(float g) { preGain = g; }
    void setCutoff(float c)
    {
        cutoff = c;
        updateFilter();
    }

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        filter.reset();
        juce::dsp::ProcessSpec spec { sampleRate, 512, 1 };
        filter.prepare(spec);
        updateFilter();
    }

    float processSample(float input)
    {
        float x = input * preGain;
        x = std::tanh(x); // soft clipping
        return filter.processSample(x);
    }

private:
    float preGain;
    float cutoff;
    double fs;

    juce::dsp::IIR::Filter<float> filter;

    void updateFilter()
    {
        const float Q = 0.707f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(fs, cutoff, Q);
        filter.coefficients = coeffs;
    }
};

class Wavefolder
{
public:
    Wavefolder() : depth(0.0f) {}

    void setDepth(float d) { depth = juce::jlimit(0.0f, 1.0f, d); }

    float processSample(float input)
    {
        // Scale input with depth to get stronger folding at higher depths
        float scaled = input * (1.0f + depth * 9.0f); // 1x → 10x
        float folded = std::sin(scaled * juce::MathConstants<float>::halfPi);
        folded = std::tanh(folded);
        return input * (1.0f - depth) + folded * depth;
    }

private:
    float depth;
};

//==============================================================================

class DREKAVACAudioProcessor  : public juce::AudioProcessor
{
public:
    DREKAVACAudioProcessor();
    ~DREKAVACAudioProcessor() override;
    
    // AudioProcessor overrides
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // convenience preset save/load
    void savePresetToFile(const juce::File& file);
    void loadPresetFromFile(const juce::File& file);
    
    juce::String getCurrentPresetName() const { return currentPresetName; }
    
    void notifyUIUpdate();

    // public APVTS for editor attachment
    juce::AudioProcessorValueTreeState parameters;

private:
    // DSP stages
    Overdrive overdrive;
    Distortion dist;
    Wavefolder fold;
    ToneProcessor toneProcessor;

    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::String currentPresetName { "Default" };
    
     // --- Oversampling ---
     juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    // 2x oversampling, stereo


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DREKAVACAudioProcessor)
};
