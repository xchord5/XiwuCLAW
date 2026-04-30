#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr int kEditorWidth = 380;
constexpr int kEditorHeight = 200;

juce::StringArray getCategoryNames()
{
    return { "Drums", "Rhythm", "Melody" };
}
}

XiwuProbePluginAudioProcessorEditor::XiwuProbePluginAudioProcessorEditor(
    XiwuProbePluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    addAndMakeVisible(titleLabel);
    titleLabel.setText("XiwuCLAW Probe", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // 分类选择
    addAndMakeVisible(categoryLabel);
    categoryLabel.setText("Category:", juce::dontSendNotification);
    categoryLabel.setFont(juce::FontOptions(12.0f));
    categoryLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(categoryComboBox);
    categoryComboBox.addItemList(getCategoryNames(), 1);
    categoryComboBox.setSelectedId(1);
    categoryComboBox.onChange = [this] {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.getParametersState().getParameter("category")))
            param->setValueNotifyingHost(static_cast<float>(categoryComboBox.getSelectedId() - 1));
    };

    // 轨道号选择 (1-64)
    addAndMakeVisible(trackNumberLabel);
    trackNumberLabel.setText("Track Number:", juce::dontSendNotification);
    trackNumberLabel.setFont(juce::FontOptions(12.0f));
    trackNumberLabel.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(trackNumberSlider);
    trackNumberSlider.setRange(1.0, 64.0, 1.0);
    trackNumberSlider.setValue(1.0, juce::dontSendNotification);
    trackNumberSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    trackNumberSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    trackNumberSlider.onValueChange = [this] {
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(audioProcessor.getParametersState().getParameter("track_number")))
            param->setValueNotifyingHost((float)((int)trackNumberSlider.getValue() - 1) / 63.0f);
    };

    addAndMakeVisible(connectionStatusLabel);
    connectionStatusLabel.setText("Status: Idle", juce::dontSendNotification);
    connectionStatusLabel.setFont(juce::FontOptions(11.0f));
    connectionStatusLabel.setJustificationType(juce::Justification::centredLeft);

    setSize(kEditorWidth, kEditorHeight);

    // 同步当前参数值
    if (auto* catParam = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.getParametersState().getParameter("category")))
        categoryComboBox.setSelectedId(static_cast<int>(catParam->getIndex()) + 1, juce::dontSendNotification);

    if (auto* numParam = dynamic_cast<juce::AudioParameterInt*>(audioProcessor.getParametersState().getParameter("track_number")))
        trackNumberSlider.setValue((double)(int)*numParam, juce::dontSendNotification);

    startTimerHz(2);
    updateStatus();
}

XiwuProbePluginAudioProcessorEditor::~XiwuProbePluginAudioProcessorEditor()
{
}

void XiwuProbePluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A2E));
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawRect(getLocalBounds(), 1);
}

void XiwuProbePluginAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(12);

    titleLabel.setBounds(bounds.removeFromTop(28));
    bounds.removeFromTop(10);

    categoryLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(4);
    categoryComboBox.setBounds(bounds.removeFromTop(26));
    bounds.removeFromTop(10);

    trackNumberLabel.setBounds(bounds.removeFromTop(18));
    bounds.removeFromTop(4);
    trackNumberSlider.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(10);

    connectionStatusLabel.setBounds(bounds.removeFromTop(20));
}

void XiwuProbePluginAudioProcessorEditor::timerCallback()
{
    updateStatus();
}

void XiwuProbePluginAudioProcessorEditor::updateStatus()
{
    const int trackNum = audioProcessor.getTrackNumber();
    juce::String status;
    if (audioProcessor.isUdpConnected())
    {
        status = "UDP: Listening (port 3922) | Track " + juce::String(trackNum);
        connectionStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    }
    else
    {
        status = "UDP: Initializing... | Track " + juce::String(trackNum);
        connectionStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    }

    connectionStatusLabel.setText(status, juce::dontSendNotification);
}
