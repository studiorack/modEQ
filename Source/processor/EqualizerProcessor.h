/*
  ==============================================================================

    EqualizerProcessor.h
    Created: 2 Sep 2018 10:59:13pm
    Author:  tobante

  ==============================================================================
*/

#pragma once
#include "../Analyser.h"
#include "../utils/TextValueConverters.h"
#include "BaseProcessor.h"
namespace TA
{
//==============================================================================
class EqualizerProcessor : public ProcessorBase, public ChangeBroadcaster, AudioProcessorValueTreeState::Listener

{
public:
  //==============================================================================
  enum FilterType
  {
    NoFilter = 0,
    HighPass,
    HighPass1st,
    LowShelf,
    BandPass,
    AllPass,
    AllPass1st,
    Notch,
    Peak,
    HighShelf,
    LowPass1st,
    LowPass,
    LastFilterID
  };

  //==============================================================================
  struct Band
  {
    String name;
    Colour colour;
    FilterType type = BandPass;
    float frequency = 1000.0f;
    float quality = 1.0f;
    float gain = 1.0f;
    bool active = true;
    std::vector<double> magnitudes;
  };

  //==============================================================================
  EqualizerProcessor(AudioProcessorValueTreeState&);
  ~EqualizerProcessor();

  //==============================================================================
  void prepareToPlay(double, int) override;
  void prepare(const dsp::ProcessSpec&);

  //==============================================================================
  void process(const dsp::ProcessContextReplacing<float>&);
  void processBlock(AudioSampleBuffer&, MidiBuffer&) override;

  //==============================================================================
  void reset() override;
  void parameterChanged(const String& parameter, float newValue) override;

  //==============================================================================
  static String getFilterTypeName(const TA::EqualizerProcessor::FilterType);
  const String getName() const override { return "Equalizer"; }
  Band* getBand(const int);

  //==============================================================================
  void updateBand(const size_t);
  void updateBypassedStates();
  void updatePlots();

  //==============================================================================
  const std::vector<double>& getMagnitudes();
  void createFrequencyPlot(Path&, const std::vector<double>&, const Rectangle<int>, float);
  void createAnalyserPlot(Path&, const Rectangle<int>, float, bool);
  bool checkForNewAnalyserData();

  //==============================================================================
  static String paramOutput;
  static String paramType;
  static String paramFrequency;
  static String paramQuality;
  static String paramGain;
  static String paramActive;

  String getTypeParamName(const int) const;
  String getFrequencyParamName(const int) const;
  String getQualityParamName(const int) const;
  String getGainParamName(const int) const;
  String getActiveParamName(const int) const;

  //==============================================================================
  int getNumBands() const;
  String getBandName(const int) const;
  Colour getBandColour(const int) const;
  void setBandSolo(const int);
  bool getBandSolo(const int) const;

  AudioProcessorValueTreeState& getPluginState() { return state; }

private:
  //==============================================================================
  AudioProcessorValueTreeState& state;

  //==============================================================================
  double sampleRate = 0;
  int soloed = -1;
  bool wasBypassed = true;

  //==============================================================================
  using FilterBand = dsp::ProcessorDuplicator<dsp::IIR::Filter<float>, dsp::IIR::Coefficients<float>>;

  dsp::ProcessorChain<FilterBand, FilterBand, FilterBand, FilterBand, FilterBand, FilterBand> filter;
  std::vector<Band> bands;

  std::vector<double> frequencies;
  std::vector<double> magnitudes;


  Analyser<float> inputAnalyser;
  Analyser<float> outputAnalyser;

  GainTextConverter gainTextConverter;
  ActiveTextConverter activeTextConverter;
  QualityTextConverter qualityTextConverter;
  FrequencyTextConverter frequencyTextConverter;
};

} // namespace TA