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
        updateCoefficients();
        lowFilter.reset();
        highFilter.reset();
    }

    void setParameters(float toneSlider, float driveSlider)
    {
        // Keep tone slider linear 0–1
        balance = juce::jlimit(0.0f, 1.0f, toneSlider);

        // Drive subtly affects filter pivot and resonance
        modulatedPivot = pivotFreq + driveSlider * 100.0f; // pivot 1 kHz → ~2 kHz at max drive
        modulatedQ = q + driveSlider * 0.05f;          // Q 0.707 → ~1.2 at max drive

        updateCoefficients();
    }

    float processSample(float input)
    {
        float low = lowFilter.processSample(input);
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

    // Base parameters
    const float pivotFreq = 1000.0f;
    const float q = 0.707f;

    // Drive-modulated parameters
    float modulatedPivot = pivotFreq;
    float modulatedQ = q;

    void updateCoefficients()
    {
        auto lowShelf = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            fs, modulatedPivot, modulatedQ, 1.0f + (1.0f - balance) * 1.5f);

        auto highShelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            fs, modulatedPivot, modulatedQ, 1.0f + balance * 1.5f);

        lowFilter.coefficients = lowShelf;
        highFilter.coefficients = highShelf;
    }
};

class Overdrive
{
public:
    Overdrive() : drive(1.0f), tone(0.5f), prevY(0.0f) {}

    void setDrive(float d) { drive = d; }
    void setTone(float t) { tone = juce::jlimit(0.0f, 1.0f, t); }

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
    Distortion() : preGain(1.0f), sliderValue(0.2f), fs(44100.0)
    {
        updateFilter();
    }

    void setPreGain(float g) { preGain = g; }

    // sliderValue expected 0.0 -> 1.0
    void setCutoffSliderValue(float value)
    {
        sliderValue = juce::jlimit(0.0f, 1.0f, value);

        const float minHz = 100.0f;
        const float maxHz = 8000.0f;
        const float exponent = 0.7f; // gentle logarithmic response
        cutoff = minHz * std::pow(maxHz / minHz, std::pow(sliderValue, exponent));

        updateFilter();
    }

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        for (auto& f : filters)
            f.reset();
        juce::dsp::ProcessSpec spec{ sampleRate, 512, 1 };
        for (auto& f : filters)
            f.prepare(spec);
        updateFilter();
    }

    float processSample(float input)
    {
        // --- Pre soft clipping ---
        float y = std::tanh(input * preGain);

        // --- 4-pole lowpass (cutoff controlled by slider) ---
        for (auto& f : filters)
            y = f.processSample(y);

        // --- Gentle one-pole post lowpass (~10 kHz) to reduce fizz ---
        const float postCutoff = 10000.0f; // Hz
        const float alpha = std::exp(-2.0f * juce::MathConstants<float>::pi * postCutoff / fs);
        y = postPrev + (1.0f - alpha) * (y - postPrev);
        postPrev = y;

        // --- Final soft clipping for smooth output limiting ---
        return std::tanh(y);
    }

private:
    float preGain;
    float sliderValue; // 0..1 slider input
    float cutoff;
    double fs;
    float postPrev = 0.0f;

    // 2x 2-pole lowpass for 4-pole response
    std::array<juce::dsp::IIR::Filter<float>, 2> filters;

    void updateFilter()
    {
        const float Q = 0.707f;
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(fs, cutoff, Q);
        for (auto& f : filters)
            f.coefficients = coeffs;
    }
};



class Wavefolder
{
public:
    Wavefolder() : depth(0.0f) {}

    void setDepth(float d) {
        depth = juce::jlimit(0.0f, 1.0f, std::pow(d, 1.5f));
    }

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

class SimpleCompressor
{
public:
    SimpleCompressor()
        : threshold(-3.0f),   // -3 dB, gentle limiting
        ratio(2.0f),        // mild compression
        knee(2.0f),         // small soft knee
        attackTime(0.005f), // 5 ms fast attack
        releaseTime(0.05f), // 50 ms release
        sampleRate(44100.0),
        envelope(0.0f)
    {
    }

    void prepare(double fs)
    {
        sampleRate = fs;
        envelope = 0.0f;

        attackCoeff = std::exp(-1.0f / (attackTime * sampleRate));
        releaseCoeff = std::exp(-1.0f / (releaseTime * sampleRate));
    }

    float processSample(float input)
    {
        // Simple one-pole envelope follower
        float level = std::fabs(input);
        if (level > envelope)
            envelope = attackCoeff * (envelope - level) + level;
        else
            envelope = releaseCoeff * (envelope - level) + level;

        // Convert to dB
        float levelDb = linearToDb(envelope);

        // Soft-knee gain reduction
        float gainDb = 0.0f;
        float lowerKnee = threshold - knee / 2.0f;
        float upperKnee = threshold + knee / 2.0f;

        if (levelDb > upperKnee)
        {
            gainDb = threshold + (levelDb - threshold) / ratio - levelDb;
        }
        else if (levelDb > lowerKnee)
        {
            float x = (levelDb - lowerKnee) / knee; // 0 → 1
            float smooth = x * x * (3.0f - 2.0f * x);    // S-curve
            gainDb = smooth * (threshold + (levelDb - threshold) / ratio - levelDb);
        }

        float gain = dbToLinear(gainDb);
        return input * gain;
    }

private:
    float threshold;
    float ratio;
    float knee;
    float attackTime;
    float releaseTime;

    double sampleRate;
    float envelope;
    float attackCoeff;
    float releaseCoeff;

    // Lightweight conversions
    static float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
    static float linearToDb(float lin) { return 20.0f * std::log10(std::max(lin, 1e-20f)); }
};


//==============================================================================

class DREKAVACAudioProcessor : public juce::AudioProcessor
{
public:
    DREKAVACAudioProcessor();
    ~DREKAVACAudioProcessor() override;

    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
    SimpleCompressor simpleComp;


    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::String currentPresetName{ "Default" };

    // --- Oversampling ---
    juce::dsp::Oversampling<float> oversampler{ 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

    // 2x oversampling, stereo


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DREKAVACAudioProcessor)
};