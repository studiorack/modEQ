/*
  ==============================================================================

    This is the Frequalizer implementation

  ==============================================================================
*/

#include "FrequalizerProcessor.h"
#include "SocialButtons.h"
#include "FrequalizerEditor.h"


String FrequalizerAudioProcessor::paramOutput   ("output");
String FrequalizerAudioProcessor::paramType     ("type");
String FrequalizerAudioProcessor::paramFrequency("frequency");
String FrequalizerAudioProcessor::paramQuality  ("quality");
String FrequalizerAudioProcessor::paramGain     ("gain");
String FrequalizerAudioProcessor::paramActive   ("active");



//==============================================================================
FrequalizerAudioProcessor::FrequalizerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  AudioChannelSet::stereo(), true)
                       .withOutput ("Output", AudioChannelSet::stereo(), true)
                       ),
#else
:
#endif
state (*this, &undo)
{
    const float maxGain = Decibels::decibelsToGain (24.0f);

    state.createAndAddParameter (paramOutput, TRANS ("Output"), TRANS ("Output level"),
                                 NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
                                 [](float value) {return String (Decibels::gainToDecibels(value), 1) + " dB";},
                                 [](String text) {return Decibels::decibelsToGain (text.dropLastCharacters (3).getFloatValue());},
                                 false, true, false);

    frequencies.resize (300);
    for (size_t i=0; i < frequencies.size(); ++i) {
        frequencies [i] = 20.0 * std::pow (2.0, i / 30.0);
    }
    magnitudes.resize (frequencies.size());

    // needs to be in sync with the ProcessorChain filter
    bands.resize (6);

    // setting defaults
    {
        auto& band = bands [0];
        band.name       = "Lowest";
        band.colour     = Colours::blue;
        band.frequency  = 20.0f;
        band.quality    = 0.707f;
        band.type       = HighPass;
    }
    {
        auto& band = bands [1];
        band.name       = "Low";
        band.colour     = Colours::brown;
        band.frequency  = 250.0f;
        band.type       = LowShelf;
    }
    {
        auto& band = bands [2];
        band.name       = "Low Mids";
        band.colour     = Colours::green;
        band.frequency  = 500.0f;
        band.type       = Peak;
    }
    {
        auto& band = bands [3];
        band.name       = "High Mids";
        band.colour     = Colours::coral;
        band.frequency  = 1000.0f;
        band.type       = Peak;
    }
    {
        auto& band = bands [4];
        band.name       = "High";
        band.colour     = Colours::orange;
        band.frequency  = 5000.0f;
        band.type       = HighShelf;
    }
    {
        auto& band = bands [5];
        band.name       = "Highest";
        band.colour     = Colours::red;
        band.frequency  = 12000.0f;
        band.quality    = 0.707f;
        band.type       = LowPass;
    }

    for (int i = 0; i < int (bands.size()); ++i) {
        auto& band = bands [size_t (i)];

        band.magnitudes.resize (frequencies.size(), 1.0);

        state.createAndAddParameter (getTypeParamName (i), band.name + " Type", TRANS ("Filter Type"),
                                     NormalisableRange<float> (0, LastFilterID, 1),
                                     (float)band.type,
                                     [](float value) { return FrequalizerAudioProcessor::getFilterTypeName (static_cast<FilterType>(static_cast<int> (value))); },
                                     [](String text) {
                                         for (int i=0; i < LastFilterID; ++i)
                                             if (text == FrequalizerAudioProcessor::getFilterTypeName (static_cast<FilterType>(i)))
                                                 return static_cast<FilterType>(i);
                                         return NoFilter; },
                                     false, true, true);

        state.createAndAddParameter (getFrequencyParamName (i), band.name + " freq", "Frequency",
                                     NormalisableRange<float> (20.0, 20000.0, 1.0),
                                     band.frequency,
                                     [](float value) { return (value < 1000) ?
                                         String (value, 0) + " Hz" :
                                         String (value / 1000.0, 2) + " kHz"; },
                                     [](String text) { return text.endsWith(" kHz") ?
                                         text.dropLastCharacters (4).getFloatValue() * 1000.0 :
                                         text.dropLastCharacters (3).getFloatValue(); },
                                     false, true, false);
        state.createAndAddParameter (getQualityParamName (i), band.name + " Q", TRANS ("Quality"),
                                     NormalisableRange<float> (0.1f, 10.0f, 0.1f),
                                     band.quality,
                                     [](float value) { return String (value, 1); },
                                     [](const String& text) { return text.getFloatValue(); },
                                     false, true, false);
        state.createAndAddParameter (getGainParamName (i), band.name + " gain", TRANS ("Gain"),
                                     NormalisableRange<float> (1.0f / maxGain, maxGain, 0.001f),
                                     band.gain,
                                     [](float value) {return String (Decibels::gainToDecibels(value), 1) + " dB";},
                                     [](String text) {return Decibels::decibelsToGain (text.dropLastCharacters (3).getFloatValue());},
                                     false, true, false);
        state.createAndAddParameter (getActiveParamName (i), band.name + " active", TRANS ("Active"),
                                     NormalisableRange<float> (0, 1, 1),
                                     band.active,
                                     [](float value) {return value > 0.5f ? TRANS ("active") : TRANS ("bypassed");},
                                     [](String text) {return text == TRANS ("active");},
                                     false, true, true);

        state.addParameterListener (getTypeParamName (i), this);
        state.addParameterListener (getFrequencyParamName (i), this);
        state.addParameterListener (getQualityParamName (i), this);
        state.addParameterListener (getGainParamName (i), this);
        state.addParameterListener (getActiveParamName (i), this);
    }

    state.addParameterListener (paramOutput, this);

    state.state = ValueTree (JucePlugin_Name);
}

FrequalizerAudioProcessor::~FrequalizerAudioProcessor()
{
    inputAnalyser.stopThread (1000);
    outputAnalyser.stopThread (1000);
}

//==============================================================================
const String FrequalizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FrequalizerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool FrequalizerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool FrequalizerAudioProcessor::isMidiEffect() const
{
    return false;
}

double FrequalizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FrequalizerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int FrequalizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FrequalizerAudioProcessor::setCurrentProgram (int)
{
}

const String FrequalizerAudioProcessor::getProgramName (int)
{
    return {};
}

void FrequalizerAudioProcessor::changeProgramName (int, const String&)
{
}

//==============================================================================
void FrequalizerAudioProcessor::prepareToPlay (double newSampleRate, int newSamplesPerBlock)
{
    sampleRate = newSampleRate;

    dsp::ProcessSpec spec;
    spec.sampleRate = newSampleRate;
    spec.maximumBlockSize = uint32 (newSamplesPerBlock);
    spec.numChannels = uint32 (getTotalNumOutputChannels ());

    for (size_t i=0; i < bands.size(); ++i) {
        updateBand (i);
    }
    filter.get<6>().setGainLinear (*state.getRawParameterValue (paramOutput));

    updatePlots();

    filter.prepare (spec);

    inputAnalyser.setupAnalyser  (int (sampleRate), float (sampleRate));
    outputAnalyser.setupAnalyser (int (sampleRate), float (sampleRate));
}

void FrequalizerAudioProcessor::releaseResources()
{
    inputAnalyser.stopThread (1000);
    outputAnalyser.stopThread (1000);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FrequalizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // This checks if the input layout matches the output layout
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void FrequalizerAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    ignoreUnused (midiMessages);

    if (getActiveEditor() != nullptr)
        inputAnalyser.addAudioData (buffer, 0, getTotalNumInputChannels());

    if (wasBypassed) {
        filter.reset();
        wasBypassed = false;
    }
    dsp::AudioBlock<float>              ioBuffer (buffer);
    dsp::ProcessContextReplacing<float> context  (ioBuffer);
    filter.process (context);

    if (getActiveEditor() != nullptr)
        outputAnalyser.addAudioData (buffer, 0, getTotalNumOutputChannels());
}

AudioProcessorValueTreeState& FrequalizerAudioProcessor::getPluginState()
{
    return state;
}

String FrequalizerAudioProcessor::getTypeParamName (const int index) const
{
    return getBandName (index) + "-" + paramType;
}

String FrequalizerAudioProcessor::getFrequencyParamName (const int index) const
{
    return getBandName (index) + "-" + paramFrequency;
}

String FrequalizerAudioProcessor::getQualityParamName (const int index) const
{
    return getBandName (index) + "-" + paramQuality;
}

String FrequalizerAudioProcessor::getGainParamName (const int index) const
{
    return getBandName (index) + "-" + paramGain;
}

String FrequalizerAudioProcessor::getActiveParamName (const int index) const
{
    return getBandName (index) + "-" + paramActive;
}

void FrequalizerAudioProcessor::parameterChanged (const String& parameter, float newValue)
{
    if (parameter == paramOutput) {
        filter.get<6>().setGainLinear (newValue);
        updatePlots();
        return;
    }

    for (size_t i=0; i < bands.size(); ++i) {
        if (parameter.startsWith (getBandName (int (i)) + "-")) {
            if (parameter.endsWith (paramType)) {
                bands [i].type = static_cast<FilterType> (static_cast<int> (newValue));
            }
            else if (parameter.endsWith (paramFrequency)) {
                bands [i].frequency = newValue;
            }
            else if (parameter.endsWith (paramQuality)) {
                bands [i].quality = newValue;
            }
            else if (parameter.endsWith (paramGain)) {
                bands [i].gain = newValue;
            }
            else if (parameter.endsWith (paramActive)) {
                bands [i].active = newValue >= 0.5f;
            }

            updateBand (i);
            return;
        }
    }
}

int FrequalizerAudioProcessor::getNumBands () const
{
    return static_cast<int> (bands.size());
}

String FrequalizerAudioProcessor::getBandName   (const int index) const
{
    if (isPositiveAndBelow (index, bands.size()))
        return bands [size_t (index)].name;
    return TRANS ("unknown");
}
Colour FrequalizerAudioProcessor::getBandColour (const int index) const
{
    if (isPositiveAndBelow (index, bands.size()))
        return bands [size_t (index)].colour;
    return Colours::silver;
}

void FrequalizerAudioProcessor::setBandSolo (const int index)
{
    soloed = index;
    updateBypassedStates();
}

void FrequalizerAudioProcessor::updateBypassedStates ()
{
    if (isPositiveAndBelow (soloed, bands.size())) {
        filter.setBypassed<0>(soloed != 0);
        filter.setBypassed<1>(soloed != 1);
        filter.setBypassed<2>(soloed != 2);
        filter.setBypassed<3>(soloed != 3);
        filter.setBypassed<4>(soloed != 4);
        filter.setBypassed<5>(soloed != 5);
    }
    else {
        filter.setBypassed<0>(!bands[0].active);
        filter.setBypassed<1>(!bands[1].active);
        filter.setBypassed<2>(!bands[2].active);
        filter.setBypassed<3>(!bands[3].active);
        filter.setBypassed<4>(!bands[4].active);
        filter.setBypassed<5>(!bands[5].active);
    }
    updatePlots();
}

bool FrequalizerAudioProcessor::getBandSolo (const int index) const
{
    return index == soloed;
}

FrequalizerAudioProcessor::Band* FrequalizerAudioProcessor::getBand (const int index)
{
    if (isPositiveAndBelow (index, bands.size()))
        return &bands [size_t (index)];
    return nullptr;
}

String FrequalizerAudioProcessor::getFilterTypeName (const FilterType type)
{
    switch (type) {
        case NoFilter:      return TRANS ("No Filter");
        case HighPass:      return TRANS ("High Pass");
        case HighPass1st:   return TRANS ("1st High Pass");
        case LowShelf:      return TRANS ("Low Shelf");
        case BandPass:      return TRANS ("Band Pass");
        case AllPass:       return TRANS ("All Pass");
        case AllPass1st:    return TRANS ("1st All Pass");
        case Notch:         return TRANS ("Notch");
        case Peak:          return TRANS ("Peak");
        case HighShelf:     return TRANS ("High Shelf");
        case LowPass1st:    return TRANS ("1st Low Pass");
        case LowPass:       return TRANS ("Low Pass");
        default:            return TRANS ("unknown");
    }
}

void FrequalizerAudioProcessor::updateBand (const size_t index)
{
    if (sampleRate > 0) {
        dsp::IIR::Coefficients<float>::Ptr newCoefficients;
        switch (bands [index].type) {
            case NoFilter:
                newCoefficients = new dsp::IIR::Coefficients<float> (1, 0, 1, 0);
                break;
            case LowPass:
                newCoefficients = dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, bands [index].frequency, bands [index].quality);
                break;
            case LowPass1st:
                newCoefficients = dsp::IIR::Coefficients<float>::makeFirstOrderLowPass (sampleRate, bands [index].frequency);
                break;
            case LowShelf:
                newCoefficients = dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, bands [index].frequency, bands [index].quality, bands [index].gain);
                break;
            case BandPass:
                newCoefficients = dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, bands [index].frequency, bands [index].quality);
                break;
            case AllPass:
                newCoefficients = dsp::IIR::Coefficients<float>::makeAllPass (sampleRate, bands [index].frequency, bands [index].quality);
                break;
            case AllPass1st:
                newCoefficients = dsp::IIR::Coefficients<float>::makeFirstOrderAllPass (sampleRate, bands [index].frequency);
                break;
            case Notch:
                newCoefficients = dsp::IIR::Coefficients<float>::makeNotch (sampleRate, bands [index].frequency, bands [index].quality);
                break;
            case Peak:
                newCoefficients = dsp::IIR::Coefficients<float>::makePeakFilter (sampleRate, bands [index].frequency, bands [index].quality, bands [index].gain);
                break;
            case HighShelf:
                newCoefficients = dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, bands [index].frequency, bands [index].quality, bands [index].gain);
                break;
            case HighPass1st:
                newCoefficients = dsp::IIR::Coefficients<float>::makeFirstOrderHighPass (sampleRate, bands [index].frequency);
                break;
            case HighPass:
                newCoefficients = dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, bands [index].frequency, bands [index].quality);
                break;
            default:
                break;
        }

        if (newCoefficients)
        {
            {
                // minimise lock scope, get<0>() needs to be a  compile time constant
                ScopedLock processLock (getCallbackLock());
                if (index == 0)
                    *filter.get<0>().state = *newCoefficients;
                else if (index == 1)
                    *filter.get<1>().state = *newCoefficients;
                else if (index == 2)
                    *filter.get<2>().state = *newCoefficients;
                else if (index == 3)
                    *filter.get<3>().state = *newCoefficients;
                else if (index == 4)
                    *filter.get<4>().state = *newCoefficients;
                else if (index == 5)
                    *filter.get<5>().state = *newCoefficients;
            }
            newCoefficients->getMagnitudeForFrequencyArray (frequencies.data(),
                                                            bands [index].magnitudes.data(),
                                                            frequencies.size(), sampleRate);

        }
        updateBypassedStates();
        updatePlots();
    }
}

void FrequalizerAudioProcessor::updatePlots ()
{
    auto gain = filter.get<6>().getGainLinear();
    std::fill (magnitudes.begin(), magnitudes.end(), gain);

    if (isPositiveAndBelow (soloed, bands.size())) {
        FloatVectorOperations::multiply (magnitudes.data(), bands [size_t (soloed)].magnitudes.data(), static_cast<int> (magnitudes.size()));
    }
    else
    {
        for (size_t i=0; i < bands.size(); ++i)
            if (bands[i].active)
                FloatVectorOperations::multiply (magnitudes.data(), bands [i].magnitudes.data(), static_cast<int> (magnitudes.size()));
    }

    sendChangeMessage();
}

//==============================================================================
bool FrequalizerAudioProcessor::hasEditor() const
{
    return true;
}

AudioProcessorEditor* FrequalizerAudioProcessor::createEditor()
{
    return new FrequalizerAudioProcessorEditor (*this);
}

const std::vector<double>& FrequalizerAudioProcessor::getMagnitudes ()
{
    return magnitudes;
}

void FrequalizerAudioProcessor::createFrequencyPlot (Path& p, const std::vector<double>& mags, const Rectangle<int> bounds, float pixelsPerDouble)
{
    p.startNewSubPath (bounds.getX(), roundToInt (bounds.getCentreY() - pixelsPerDouble * std::log (mags [0]) / std::log (2)));
    const double xFactor = static_cast<double> (bounds.getWidth()) / frequencies.size();
    for (size_t i=1; i < frequencies.size(); ++i)
    {
        p.lineTo (roundToInt (bounds.getX() + i * xFactor),
                  roundToInt (bounds.getCentreY() - pixelsPerDouble * std::log (mags [i]) / std::log (2)));
    }
}

void FrequalizerAudioProcessor::createAnalyserPlot (Path& p, const Rectangle<int> bounds, float minFreq, bool input)
{
    if (input)
        inputAnalyser.createPath (p, bounds.toFloat(), minFreq);
    else
        outputAnalyser.createPath (p, bounds.toFloat(), minFreq);
}

bool FrequalizerAudioProcessor::checkForNewAnalyserData()
{
    return inputAnalyser.checkForNewData() || outputAnalyser.checkForNewData();
}

//==============================================================================
void FrequalizerAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    //MemoryOutputStream stream(destData, false);
    //state.state.writeToStream (stream);
}

void FrequalizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    //ValueTree tree = ValueTree::readFromData (data, size_t (sizeInBytes));
    //if (tree.isValid()) {
    //    state.state = tree;
    //}
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrequalizerAudioProcessor();
}
