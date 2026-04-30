#pragma once

#include "PluginProcessor.h"

class XiwuProbePluginAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit XiwuProbePluginAudioProcessorEditor(XiwuProbePluginAudioProcessor&);
    ~XiwuProbePluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateStatus();

    XiwuProbePluginAudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::Label categoryLabel;
    juce::Label trackNumberLabel;
    juce::Label connectionStatusLabel;

    juce::ComboBox categoryComboBox;
    juce::Slider trackNumberSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XiwuProbePluginAudioProcessorEditor)
};
