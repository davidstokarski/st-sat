#include "PluginProcessor.h"
StSatProcessor::StSatProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) {}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StSatProcessor(); }
