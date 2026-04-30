#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <vector>
#include <cstdint>
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace
{
constexpr const char* kProbeSessionDirName = "XiwuCLAW";
constexpr const char* kProbeSessionFileName = "probe_session.json";
constexpr const char* kProbeLogFileName = "probe_debug.log";

juce::StringArray getCategoryNames()
{
    return { "Drums", "Rhythm", "Melody" };
}

juce::StringArray getTrackTagPresets()
{
    return { "Track1", "Track2", "Track3", "Track4", "Track5",
             "Vocal", "Piano", "Drums", "Bass", "Guitar",
             "Synth", "Pad", "Lead", "Pluck", "FX" };
}
} // namespace

// ============================================================================
// Helper Functions
// ============================================================================
juce::File getProbeSessionFile()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile(kProbeSessionDirName);
    return dir.getChildFile(kProbeSessionFileName);
}

int XiwuProbePluginAudioProcessor::readPortFromSessionFile()
{
    const auto sessionFile = getProbeSessionFile();
    if (sessionFile.existsAsFile())
    {
        if (auto json = juce::JSON::parse(sessionFile.loadFileAsString()))
        {
            if (auto* obj = dynamic_cast<juce::DynamicObject*>(json.getDynamicObject()))
            {
                if (obj->hasProperty("port"))
                    return obj->getProperty("port");
            }
        }
    }
    return 3922;  // Default port
}

// ============================================================================
// XiwuProbePluginAudioProcessor
// ============================================================================
XiwuProbePluginAudioProcessor::XiwuProbePluginAudioProcessor()
    : AudioProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, juce::Identifier("XiwuProbe"), createParameterLayout())
    , category("Drums")
    , trackNumber(1)
{
    midiTracker = std::make_unique<MidiActivityTracker>();
    probeId = juce::Uuid();  // 在构造函数体内初始化

    // 创建 UDP 线程
    midiTracker = std::make_unique<MidiActivityTracker>();
    probeId = juce::Uuid();

    udpThread = std::make_unique<UdpThread>();
    const int port = readPortFromSessionFile();
    probeLog("Probe: Starting UDP on port " + juce::String(port));
    udpThread->start(port, midiTracker.get(), this);
}

XiwuProbePluginAudioProcessor::~XiwuProbePluginAudioProcessor()
{
    udpThread.reset();
}

juce::AudioProcessorEditor* XiwuProbePluginAudioProcessor::createEditor()
{
    return new XiwuProbePluginAudioProcessorEditor(*this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
XiwuProbePluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    const auto catNames = getCategoryNames();
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("category", 1),
        "Category",
        catNames,
        0));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("track_number", 1),
        "Track Number",
        1,  // min
        64, // max
        1)); // default

    return { params.begin(), params.end() };
}

void XiwuProbePluginAudioProcessor::prepareToPlay(double, int)
{
    midiTracker->reset();
}

void XiwuProbePluginAudioProcessor::releaseResources()
{
}

bool XiwuProbePluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return output == juce::AudioChannelSet::mono()
        || output == juce::AudioChannelSet::stereo();
}

void XiwuProbePluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto currentTimeMs = juce::Time::getMillisecondCounterHiRes();

    // 处理 MIDI 消息，统计活动
    midiTracker->processMidiMessages(midiMessages, currentTimeMs);

    // 更新 category 和 track_number (只读操作，安全)
    if (auto* catParam = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("category")))
        category = getCategoryNames()[catParam->getIndex()];

    if (auto* numParam = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("track_number")))
        trackNumber = (int)*numParam;

    // 音频直通：JUCE 自动处理音频传递（Monitor 模式）
    // 我们只需要确保未使用的输出通道被清零
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // MIDI 消息已经由 midiTracker->processMidiMessages() 处理
    // JUCE 会自动将输入 MIDI 传递到输出（因为 producesMidi=false）
}

void XiwuProbePluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("track_number", juce::var(trackNumber));
    root->setProperty("category", juce::var(category));
    root->setProperty("probe_id", juce::var(probeId.toString()));

    const auto json = juce::JSON::toString(juce::var(root.get()));
    destData.append(json.toUTF8(), json.getNumBytesAsUTF8());
}

void XiwuProbePluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto json = juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes)))
    {
        if (auto* obj = dynamic_cast<juce::DynamicObject*>(json.getDynamicObject()))
        {
            if (obj->hasProperty("track_number"))
            {
                trackNumber = juce::jlimit(1, 64, (int)obj->getProperty("track_number"));
                if (auto* param = dynamic_cast<juce::AudioParameterInt*>(parameters.getParameter("track_number")))
                    param->setValueNotifyingHost((float)(trackNumber - 1) / 63.0f);
            }
            if (obj->hasProperty("category"))
            {
                category = obj->getProperty("category").toString();
                const auto catNames = getCategoryNames();
                for (int i = 0; i < catNames.size(); ++i)
                {
                    if (catNames[i] == category)
                    {
                        if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("category")))
                        {
                            param->setValueNotifyingHost(static_cast<float>(i));
                        }
                        break;
                    }
                }
            }
            if (obj->hasProperty("probe_id"))
            {
                const auto idStr = obj->getProperty("probe_id").toString();
                if (idStr.isNotEmpty())
                    probeId = juce::Uuid(idStr);
            }
        }
    }
}

// ============================================================================
// Audio Processor Creator
// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XiwuProbePluginAudioProcessor();
}
