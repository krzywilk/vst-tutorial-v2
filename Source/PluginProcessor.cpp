/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;

    settings.lowCutFreq = apvts.getRawParameterValue("LowCutFreq")->load();
    settings.highCutFreq = apvts.getRawParameterValue("HighCutFreq")->load();
    settings.peakFreq = apvts.getRawParameterValue("PeakFreq")->load();
    settings.peakGainInDecibels = apvts.getRawParameterValue("PeakGain")->load();
    settings.peakQuality = apvts.getRawParameterValue("PeakQuality")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCutSlope")->load());
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCutSlope")->load());

    //settings.lowCutBypassed = apvts.getRawParameterValue("LowCutBypassed")->load() > 0.5f;
    //settings.peakBypassed = apvts.getRawParameterValue("PeakBypassed")->load() > 0.5f;
    //settings.highCutBypassed = apvts.getRawParameterValue("HighCutBypassed")->load() > 0.5f;

    return settings;
}


//==============================================================================
Vsttutorialv2AudioProcessor::Vsttutorialv2AudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

Vsttutorialv2AudioProcessor::~Vsttutorialv2AudioProcessor()
{
}

//==============================================================================
const juce::String Vsttutorialv2AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool Vsttutorialv2AudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool Vsttutorialv2AudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool Vsttutorialv2AudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double Vsttutorialv2AudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int Vsttutorialv2AudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int Vsttutorialv2AudioProcessor::getCurrentProgram()
{
    return 0;
}

void Vsttutorialv2AudioProcessor::setCurrentProgram (int index)
{
}

const juce::String Vsttutorialv2AudioProcessor::getProgramName (int index)
{
    return {};
}

void Vsttutorialv2AudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void Vsttutorialv2AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    spec.sampleRate = sampleRate;
    leftChain.prepare(spec);
    rightChain.prepare(spec);

    auto chainSettings = getChainSettings(apvts);
    
    updatePeakFilter(chainSettings);

    auto highpassCoefs = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq, sampleRate, 2 * (1+chainSettings.lowCutSlope));
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();

    updateCutFilter(leftLowCut, highpassCoefs, chainSettings.lowCutSlope);

    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();

    updateCutFilter(rightLowCut, highpassCoefs, chainSettings.lowCutSlope);

    auto lowpassCoefs = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.highCutFreq, sampleRate, 2 * (1 + chainSettings.highCutSlope));
    auto& lefthighCut = leftChain.get<ChainPositions::HighCut>();

    updateCutFilter(lefthighCut, lowpassCoefs, chainSettings.highCutSlope);

    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    updateCutFilter(rightHighCut, lowpassCoefs, chainSettings.highCutSlope);
}

void Vsttutorialv2AudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool Vsttutorialv2AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void Vsttutorialv2AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto chainSettings = getChainSettings(apvts);

    //TODO: 
    auto highpassCutCoefs = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(chainSettings.lowCutFreq, getSampleRate(), 2 * (1 + chainSettings.lowCutSlope));
    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    updateCutFilter(leftLowCut, highpassCutCoefs, chainSettings.lowCutSlope);
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();
    updateCutFilter(rightLowCut, highpassCutCoefs, chainSettings.lowCutSlope);


    auto lowpassCoefs = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.highCutFreq, getSampleRate(), 2 * (1 + chainSettings.highCutSlope));
    auto& lefthighCut = leftChain.get<ChainPositions::HighCut>();
    updateCutFilter(lefthighCut, lowpassCoefs, chainSettings.highCutSlope);
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();
    updateCutFilter(rightHighCut, lowpassCoefs, chainSettings.highCutSlope);

    updatePeakFilter(chainSettings);



    juce::dsp::AudioBlock<float> block(buffer);
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);
}

//==============================================================================
bool Vsttutorialv2AudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* Vsttutorialv2AudioProcessor::createEditor()
{
    return new Vsttutorialv2AudioProcessorEditor (*this);
    //return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void Vsttutorialv2AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void Vsttutorialv2AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
        //TODO
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout Vsttutorialv2AudioProcessor::createParaneterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique <juce::AudioParameterFloat>("LowCutFreq","LowCutFreq",
        juce::NormalisableRange<float>(20.f,20000.f,1.f, 0.25f),20.f));
    layout.add(std::make_unique <juce::AudioParameterFloat>("HighCutFreq", "HighCutFreq",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 20000.f));
    layout.add(std::make_unique <juce::AudioParameterFloat>("PeakFreq", "PeakFreq",
        juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 750.f));
    layout.add(std::make_unique <juce::AudioParameterFloat>("PeakGain", "PeakGain",
        juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f), 0.0f));
    layout.add(std::make_unique <juce::AudioParameterFloat>("PeakQuality", "PeakQuality",
        juce::NormalisableRange<float>(0.1, 10.f, 0.05f, 1.f), 1.f));

    juce::StringArray choiceArray;
    for (int i = 0; i < 4; i++)
    {
        juce::String str(12 + i * 12);//TODO
        str << " db/Oct";
        choiceArray.add(str);
    }
    layout.add(std::make_unique <juce::AudioParameterChoice>("LowCutSlope", "LowCutSlope",choiceArray, 0));
    layout.add(std::make_unique <juce::AudioParameterChoice>("HighCutSlope", "HighCutSlope",choiceArray, 0));
    return layout;
}

void Vsttutorialv2AudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
    //auto peakCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(getSampleRate(), chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));
    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}



//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Vsttutorialv2AudioProcessor();
}

void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
    *old = *replacements;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate, chainSettings.peakFreq, chainSettings.peakQuality, juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels));
}
