#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>
#if JUCE_WINDOWS
#include <windows.h>
#endif

// Simple debug log function for main plugin
static void mainPluginLog(const juce::String& msg)
{
    juce::File logFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("XiwuCLAW")
                             .getChildFile("main_debug.log");
    logFile.appendText(juce::Time::getCurrentTime().formatted("%H:%M:%S") + " " + msg + "\n");
}

namespace XiwuToneData
{
extern const char* namedResourceList[];
extern const int namedResourceListSize;
const char* getNamedResource(const char* resourceNameUTF8, int& dataSizeInBytes);
}

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
constexpr int kGatewayOscPort = 3921;
constexpr int kOscReadBufferSize = 64 * 1024;
constexpr int kOscPollTimeoutMs = 120;
constexpr const char* kVstPrefix = "/xiwu/vst";
constexpr double kExternalBusyTimeoutMs = 210000.0;
constexpr double kProbeTtlMs = 3000.0;
constexpr const char* kProbeSessionDirName = "XiwuCLAW";
constexpr const char* kProbeSessionFileName = "probe_session.json";

juce::File getProbeSessionFile()
{
  auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile(kProbeSessionDirName);
  dir.createDirectory();
  return dir.getChildFile(kProbeSessionFileName);
}

juce::String makeClipboardToastText()
{
  return juce::String(
    L"\u5df2\u751f\u6210 MIDI\u3002\u7ed3\u679c\u5df2\u8d70\u526a\u8d34\u677f\u94fe\u8def\u3002\n"
    L"\u8bf7\u5728\u5bbf\u4e3b\u91cc\u624b\u52a8\u7c98\u8d34\u5230\u76ee\u6807\u8f68\u9053\u3002");
}

float readFloatOr(const juce::var& source, const juce::Identifier& key, float fallback)
{
  if (auto* obj = source.getDynamicObject())
  {
    if (obj->hasProperty(key))
    {
      return static_cast<float>(obj->getProperty(key));
    }
  }
  return fallback;
}

juce::String trimOscPrefix(const juce::String& address)
{
  if (address.startsWith(kVstPrefix))
  {
    auto trimmed = address.fromFirstOccurrenceOf(kVstPrefix, false, false);
    return trimmed.isEmpty() ? "/" : trimmed;
  }
  return address;
}

double readJsonDouble(const juce::var& source, const juce::Identifier& key, double fallback)
{
  if (auto* obj = source.getDynamicObject())
  {
    if (obj->hasProperty(key))
      return static_cast<double>(obj->getProperty(key));
  }
  return fallback;
}

int readJsonInt(const juce::var& source, const juce::Identifier& key, int fallback)
{
  if (auto* obj = source.getDynamicObject())
  {
    if (obj->hasProperty(key))
      return static_cast<int>(obj->getProperty(key));
  }
  return fallback;
}

juce::String readJsonString(
  const juce::var& source,
  const juce::Identifier& key,
  const juce::String& fallback = {})
{
  if (auto* obj = source.getDynamicObject())
  {
    if (obj->hasProperty(key))
      return obj->getProperty(key).toString();
  }
  return fallback;
}
} // namespace

class GainPluginAudioProcessor::OscGatewayBridge final : private juce::Thread
{
public:
  explicit OscGatewayBridge(GainPluginAudioProcessor& ownerIn)
    : juce::Thread("XiwuGatewayOscBridge"), owner(ownerIn), socket(false)
  {
    socket.bindToPort(kGatewayOscPort);
    startThread();
  }

  ~OscGatewayBridge() override
  {
    signalThreadShouldExit();
    socket.shutdown();
    waitForThreadToExit(1500);
  }

private:
  void run() override
  {
    juce::HeapBlock<char> buffer(kOscReadBufferSize);
    while (!threadShouldExit())
    {
      const auto ready = socket.waitUntilReady(true, kOscPollTimeoutMs);
      if (ready <= 0)
        continue;

      juce::String senderIp;
      int senderPort = 0;
      const auto bytes = socket.read(buffer.getData(), kOscReadBufferSize, false, senderIp, senderPort);
      if (bytes <= 0)
        continue;

      juce::String address;
      juce::String payloadArg;
      if (!GainPluginAudioProcessor::decodeOscAddressAndStringArg(
            buffer.getData(),
            static_cast<size_t>(bytes),
            address,
            payloadArg))
      {
        continue;
      }

      owner.markOscIngress(address, payloadArg, senderIp, senderPort);
      const auto response = owner.handleOscCommand(address, payloadArg);
      if (response.isNotEmpty())
      {
        socket.write(senderIp, senderPort, response.toRawUTF8(), static_cast<int>(response.getNumBytesAsUTF8()));
      }
    }
  }

  GainPluginAudioProcessor& owner;
  juce::DatagramSocket socket;
};

// UDP-based probe bridge - simpler than NamedPipe
class GainPluginAudioProcessor::ProbeUdpBridge final : private juce::Thread
{
public:
  explicit ProbeUdpBridge(GainPluginAudioProcessor& ownerIn, int portIn)
      : juce::Thread("XiwuProbeUdpBridge"), owner(ownerIn), port(portIn), socket(false), bound(false)
  {
    mainPluginLog("ProbeUdpBridge: Binding to port " + juce::String(port));
    if (socket.bindToPort(port))
    {
      bound = true;
      mainPluginLog("ProbeUdpBridge: Bound to port " + juce::String(port) + ", starting thread");
      startThread();
    }
    else
    {
      mainPluginLog("ProbeUdpBridge: Failed to bind to port " + juce::String(port));
    }
  }

  ~ProbeUdpBridge() override
  {
    signalThreadShouldExit();
    socket.shutdown();
    waitForThreadToExit(1200);
  }

  bool isReady() const { return bound; }

private:
  void run() override
  {
    mainPluginLog("ProbeUdpBridge: Thread started, receiving UDP...");
    juce::HeapBlock<char> buffer(8192);
    while (!threadShouldExit())
    {
      juce::String senderIp;
      int senderPort = 0;
      const auto bytes = socket.read(buffer.getData(), 8192, false, senderIp, senderPort);
      if (bytes > 0)
      {
        auto jsonStr = juce::String::fromUTF8(buffer.getData(), bytes);
        auto parsed = juce::JSON::parse(jsonStr);
        if (!parsed.isVoid())
        {
          owner.ingestProbeStateMessage(parsed);
        }
      }
      else
      {
        // No data, sleep a bit
        juce::Thread::sleep(10);
      }
    }
  }

  GainPluginAudioProcessor& owner;
  int port;
  juce::DatagramSocket socket;
  bool bound;
};

GainPluginAudioProcessor::XiwuVoice::XiwuVoice(
  std::atomic<float>* toneModeParamIn,
  std::atomic<float>* masterGainParamIn)
  : toneModeParam(toneModeParamIn),
    masterGainParam(masterGainParamIn)
{
  updateEnvelopeForTone(getFactoryTones().front());
}

bool GainPluginAudioProcessor::XiwuVoice::canPlaySound(juce::SynthesiserSound* sound)
{
  return dynamic_cast<XiwuSound*>(sound) != nullptr;
}

void GainPluginAudioProcessor::XiwuVoice::updateEnvelopeForTone(const ToneSpec& tone)
{
  adsrParams.attack = juce::jlimit(0.001f, 3.0f, tone.attack);
  adsrParams.decay = juce::jlimit(0.001f, 5.0f, tone.decay);
  adsrParams.sustain = juce::jlimit(0.0f, 1.0f, tone.sustain);
  adsrParams.release = juce::jlimit(0.001f, 8.0f, tone.release);
  adsr.setParameters(adsrParams);
}

void GainPluginAudioProcessor::XiwuVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
  const auto freqHz = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
  angleDelta = freqHz * kTwoPi / static_cast<float>(getSampleRate());
  angle = 0.0f;
  level = velocity;

  const auto& tones = getFactoryTones();
  const auto toneIndex = juce::jlimit(0, static_cast<int>(tones.size()) - 1, static_cast<int>(toneModeParam->load()));
  updateEnvelopeForTone(tones[static_cast<size_t>(toneIndex)]);

  adsr.noteOn();
}

void GainPluginAudioProcessor::XiwuVoice::stopNote(float, bool allowTailOff)
{
  if (allowTailOff)
  {
    adsr.noteOff();
  }
  else
  {
    adsr.reset();
    clearCurrentNote();
    angleDelta = 0.0f;
  }
}

float GainPluginAudioProcessor::XiwuVoice::renderWaveSample(float phase)
{
  const auto& tones = getFactoryTones();
  const auto toneIndex = juce::jlimit(0, static_cast<int>(tones.size()) - 1, static_cast<int>(toneModeParam->load()));
  const auto& tone = tones[static_cast<size_t>(toneIndex)];

  const auto norm = phase / kTwoPi;
  const auto sine = std::sin(phase);
  const auto saw = (2.0f * norm) - 1.0f;
  const auto square = std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
  const auto triangle = (2.0f * std::abs(2.0f * norm - 1.0f)) - 1.0f;
  const auto sub = std::sin(phase * 0.5f) >= 0.0f ? 1.0f : -1.0f;
  const auto noise = (random.nextFloat() * 2.0f) - 1.0f;

  auto sample = 0.0f;
  sample += tone.oscSine * sine;
  sample += tone.oscSub * sub;
  sample += tone.oscSaw * saw;
  sample += tone.oscSquare * square;
  sample += tone.oscTriangle * triangle;
  sample += tone.oscNoise * noise * 0.35f;

  sample *= 0.30f;
  sample = std::tanh(sample * (1.0f + tone.drive * 3.8f));

  const auto brightBlend = juce::jlimit(0.0f, 1.0f, tone.brightness);
  return sample * (0.82f + brightBlend * 0.52f);
}

void GainPluginAudioProcessor::XiwuVoice::renderNextBlock(
  juce::AudioBuffer<float>& outputBuffer,
  int startSample,
  int numSamples)
{
  if (angleDelta <= 0.0f)
  {
    return;
  }

  const auto& tones = getFactoryTones();
  const auto toneIndex = juce::jlimit(0, static_cast<int>(tones.size()) - 1, static_cast<int>(toneModeParam->load()));
  const auto& tone = tones[static_cast<size_t>(toneIndex)];

  const auto masterGain = juce::jlimit(0.0f, 1.4f, masterGainParam->load());
  const auto width = juce::jlimit(0.0f, 1.0f, tone.stereoWidth);
  const auto leftFactor = 1.0f - (width * 0.32f);
  const auto rightFactor = 1.0f + (width * 0.32f);

  for (auto i = 0; i < numSamples; ++i)
  {
    const auto env = adsr.getNextSample();
    const auto mono = renderWaveSample(angle) * level * masterGain * env;
    const auto left = mono * leftFactor;
    const auto right = mono * rightFactor;

    outputBuffer.addSample(0, startSample + i, left);
    if (outputBuffer.getNumChannels() > 1)
    {
      outputBuffer.addSample(1, startSample + i, right);
    }

    angle += angleDelta;
    if (angle >= kTwoPi)
    {
      angle -= kTwoPi;
    }

    if (!adsr.isActive())
    {
      clearCurrentNote();
      angleDelta = 0.0f;
      break;
    }
  }
}

const std::vector<GainPluginAudioProcessor::ToneSpec>& GainPluginAudioProcessor::getFactoryTones()
{
  static const auto tones = []() {
    std::vector<ToneSpec> parsed;
    std::set<std::string> seenIds;

    auto parseToneFromJson =
      [](const juce::var& parsedJson, const juce::String& fallbackName, const juce::String& sourceHint) {
        auto* root = parsedJson.getDynamicObject();
        if (root == nullptr)
          return std::optional<ToneSpec>{};

        ToneSpec tone;
        tone.name = fallbackName;
        tone.id = ("user_" + fallbackName.toLowerCase().replaceCharacter(' ', '_').replaceCharacter('-', '_'))
                    .retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789_");
        tone.source = sourceHint;
        tone.category = "Lead"; // default

        if (root->getProperty("spec_version").toString() == "tone_spec_v1")
        {
          tone.id = root->getProperty("id").toString();
          tone.name = root->getProperty("name").toString();
          tone.source = root->getProperty("source").toString();
          tone.category = root->getProperty("category").toString();
          if (tone.category.isEmpty()) {
            // Infer category from tone_type or name
            const auto toneType = root->getProperty("tone_type").toString().toLowerCase();
            const auto nameLower = tone.name.toLowerCase();
            if (toneType.contains("bass") || nameLower.contains("bass")) tone.category = "Bass";
            else if (toneType.contains("lead") || nameLower.contains("lead")) tone.category = "Lead";
            else if (toneType.contains("pad") || nameLower.contains("pad")) tone.category = "Pad";
            else if (toneType.contains("pluck") || nameLower.contains("pluck")) tone.category = "Pluck";
            else if (toneType.contains("fx") || nameLower.contains("fx")) tone.category = "FX";
            else tone.category = "Lead";
          }

          const auto osc = root->getProperty("osc");
          tone.oscSine = readFloatOr(osc, "sine", tone.oscSine);
          tone.oscSaw = readFloatOr(osc, "saw", tone.oscSaw);
          tone.oscSquare = readFloatOr(osc, "square", tone.oscSquare);
          tone.oscTriangle = readFloatOr(osc, "triangle", tone.oscTriangle);
          tone.oscNoise = readFloatOr(osc, "noise", tone.oscNoise);
          tone.oscSub = readFloatOr(osc, "sub_octave", tone.oscSub);

          const auto env = root->getProperty("env");
          tone.attack = readFloatOr(env, "attack", tone.attack);
          tone.decay = readFloatOr(env, "decay", tone.decay);
          tone.sustain = readFloatOr(env, "sustain", tone.sustain);
          tone.release = readFloatOr(env, "release", tone.release);

          const auto character = root->getProperty("character");
          tone.drive = readFloatOr(character, "drive", tone.drive);
          tone.brightness = readFloatOr(character, "brightness", tone.brightness);
          tone.stereoWidth = readFloatOr(character, "stereo_width", tone.stereoWidth);

          const auto eq4 = root->getProperty("eq4");
          tone.eqLow = readFloatOr(eq4, "low", tone.eqLow);
          tone.eqLowMid = readFloatOr(eq4, "low_mid", tone.eqLowMid);
          tone.eqHighMid = readFloatOr(eq4, "high_mid", tone.eqHighMid);
          tone.eqHigh = readFloatOr(eq4, "high", tone.eqHigh);
          return std::optional<ToneSpec>{tone};
        }

        // userlib fallback format (tone_fallback_bank JSON):
        // no full oscillator schema, so map by tone_type to usable internal template.
        const auto toneType = root->getProperty("tone_type").toString().toLowerCase();
        if (toneType.isEmpty())
          return std::optional<ToneSpec>{};

        // Set category from tone_type
        if (toneType.contains("bass")) {
          tone.category = "Bass";
          tone.oscSine = 0.18f;
          tone.oscSub = 0.72f;
          tone.oscSaw = 0.36f;
          tone.oscSquare = 0.18f;
          tone.oscTriangle = 0.10f;
          tone.attack = 0.008f;
          tone.decay = 0.16f;
          tone.sustain = 0.78f;
          tone.release = 0.22f;
          tone.drive = 0.44f;
          tone.brightness = 0.30f;
          tone.stereoWidth = 0.08f;
        }
        else if (toneType.contains("lead")) {
          tone.category = "Lead";
          tone.oscSine = 0.20f;
          tone.oscSub = 0.16f;
          tone.oscSaw = 0.58f;
          tone.oscSquare = 0.22f;
          tone.oscTriangle = 0.10f;
          tone.attack = 0.004f;
          tone.decay = 0.20f;
          tone.sustain = 0.62f;
          tone.release = 0.24f;
          tone.drive = 0.36f;
          tone.brightness = 0.66f;
          tone.stereoWidth = 0.18f;
        }
        else if (toneType.contains("pad")) {
          tone.category = "Pad";
          tone.oscSine = 0.42f;
          tone.oscSub = 0.22f;
          tone.oscSaw = 0.22f;
          tone.oscSquare = 0.08f;
          tone.oscTriangle = 0.30f;
          tone.attack = 0.18f;
          tone.decay = 0.46f;
          tone.sustain = 0.72f;
          tone.release = 0.70f;
          tone.drive = 0.14f;
          tone.brightness = 0.52f;
          tone.stereoWidth = 0.34f;
        }
        else if (toneType.contains("pluck")) {
          tone.category = "Pluck";
          tone.oscSine = 0.10f;
          tone.oscSub = 0.16f;
          tone.oscSaw = 0.44f;
          tone.oscSquare = 0.26f;
          tone.oscTriangle = 0.08f;
          tone.attack = 0.002f;
          tone.decay = 0.12f;
          tone.sustain = 0.22f;
          tone.release = 0.11f;
          tone.drive = 0.28f;
          tone.brightness = 0.70f;
          tone.stereoWidth = 0.14f;
        }
        else if (toneType.contains("fx")) {
          tone.category = "FX";
          tone.oscSine = 0.06f;
          tone.oscSub = 0.04f;
          tone.oscSaw = 0.26f;
          tone.oscSquare = 0.14f;
          tone.oscTriangle = 0.08f;
          tone.oscNoise = 0.70f;
          tone.attack = 0.010f;
          tone.decay = 0.34f;
          tone.sustain = 0.30f;
          tone.release = 0.46f;
          tone.drive = 0.50f;
          tone.brightness = 0.74f;
          tone.stereoWidth = 0.48f;
        }
        else {
          tone.category = "Lead";
        }

        const auto eq4 = root->getProperty("eq4");
        tone.eqLow = readFloatOr(eq4, "low", tone.eqLow);
        tone.eqLowMid = readFloatOr(eq4, "low_mid", tone.eqLowMid);
        tone.eqHighMid = readFloatOr(eq4, "high_mid", tone.eqHighMid);
        tone.eqHigh = readFloatOr(eq4, "high", tone.eqHigh);
        tone.id = "userlib_" + tone.id;
        tone.source = "userlib/" + toneType;
        return std::optional<ToneSpec>{tone};
      };

    auto appendTone = [&](const ToneSpec& tone) {
      if (tone.name.isEmpty())
        return;
      auto id = tone.id.isNotEmpty() ? tone.id : tone.name;
      if (id.isEmpty())
        return;
      const auto key = id.toLowerCase().toStdString();
      if (seenIds.find(key) != seenIds.end())
        return;
      seenIds.insert(key);
      parsed.push_back(tone);
    };

    for (int i = 0; i < XiwuToneData::namedResourceListSize; ++i)
    {
      const auto* resourceName = XiwuToneData::namedResourceList[i];
      if (resourceName == nullptr)
      {
        continue;
      }

      const auto name = juce::String::fromUTF8(resourceName);
      if (!name.endsWithIgnoreCase(".json"))
      {
        continue;
      }

      int dataSize = 0;
      const auto* bytes = XiwuToneData::getNamedResource(resourceName, dataSize);
      if (bytes == nullptr || dataSize <= 0)
      {
        continue;
      }

      const auto text = juce::String::fromUTF8(bytes, dataSize);
      const auto parsedJson = juce::JSON::parse(text);
      if (const auto parsedTone = parseToneFromJson(parsedJson, name.upToLastOccurrenceOf(".", false, false), "factory"))
        appendTone(*parsedTone);
    }

    juce::Array<juce::File> userlibRoots;
    const auto envUserLib = juce::SystemStats::getEnvironmentVariable("XIWU_TONE_USERLIB_DIR", {});
    if (envUserLib.isNotEmpty())
      userlibRoots.add(juce::File(envUserLib));

    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const auto workspace = home.getChildFile(".xiwuclaw").getChildFile("workspace");
    userlibRoots.add(workspace.getChildFile("tone_fallback_bank"));
    userlibRoots.add(workspace.getChildFile("tone_userlib"));

    for (const auto& root : userlibRoots)
    {
      if (!root.exists())
        continue;

      juce::Array<juce::File> jsonFiles;
      root.findChildFiles(jsonFiles, juce::File::findFiles, true, "*.json");
      std::vector<juce::File> sortedFiles;
      sortedFiles.reserve(static_cast<size_t>(jsonFiles.size()));
      for (const auto& f : jsonFiles)
        sortedFiles.push_back(f);
      std::sort(sortedFiles.begin(), sortedFiles.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0;
      });

      for (const auto& f : sortedFiles)
      {
        auto name = f.getFileNameWithoutExtension();
        if (name.startsWithIgnoreCase("manifest") || name.containsIgnoreCase("index"))
          continue;

        const auto text = f.loadFileAsString();
        if (text.isEmpty())
          continue;
        const auto parsedJson = juce::JSON::parse(text);
        const auto source = "userlib:" + root.getFileName();
        if (const auto parsedTone = parseToneFromJson(parsedJson, name, source))
          appendTone(*parsedTone);
      }
    }

    auto toneCategoryRank = [](const ToneSpec& tone) {
      const auto id = tone.id.toLowerCase();
      if (id.startsWith("bass_"))
        return 0;
      if (id.startsWith("lead_"))
        return 1;
      if (id.startsWith("pad_"))
        return 2;
      if (id.startsWith("pluck_"))
        return 3;
      if (id.startsWith("fx_"))
        return 4;
      return 5;
    };

    std::sort(parsed.begin(), parsed.end(), [&](const ToneSpec& a, const ToneSpec& b) {
      const auto rankA = toneCategoryRank(a);
      const auto rankB = toneCategoryRank(b);
      if (rankA != rankB)
        return rankA < rankB;
      return a.name.compareNatural(b.name) < 0;
    });

    if (parsed.empty())
    {
      ToneSpec fallback;
      fallback.id = "fallback_sub";
      fallback.name = "Fallback Sub";
      parsed.push_back(fallback);
    }

    return parsed;
  }();

  return tones;
}

GainPluginAudioProcessor::GainPluginAudioProcessor()
  : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
  toneModeParam = parameters.getRawParameterValue("tone_mode");
  masterGainParam = parameters.getRawParameterValue("master_gain");
  eqLowParam = parameters.getRawParameterValue("eq_low");
  eqLowMidParam = parameters.getRawParameterValue("eq_low_mid");
  eqHighMidParam = parameters.getRawParameterValue("eq_high_mid");
  eqHighParam = parameters.getRawParameterValue("eq_high");

  // Pre-initialize tone library on construction (avoid file I/O on first UI click in VST3)
  getFactoryTones();

  for (auto i = 0; i < 14; ++i)
  {
    synth.addVoice(new XiwuVoice(toneModeParam, masterGainParam));
  }

  synth.addSound(new XiwuSound());
  oscBridge = std::make_unique<OscGatewayBridge>(*this);

  // UDP-based probe communication
  const int probePort = 3922;
  probeUdpPort = probePort;
  probeBridge = std::make_unique<ProbeUdpBridge>(*this, probePort);

  // Write session file with port info
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("ok", true);
  root->setProperty("port", probePort);
  root->setProperty("host_pid", static_cast<int>(::GetCurrentProcessId()));
  root->setProperty("created_ms", juce::Time::getMillisecondCounterHiRes());
  root->setProperty("created_iso", juce::Time::getCurrentTime().toString(true, true));
  auto json = juce::JSON::toString(juce::var(root.get()), true);
  probeSessionFile = getProbeSessionFile();
  probeSessionFile.replaceWithText(json, false, false);
}

GainPluginAudioProcessor::~GainPluginAudioProcessor()
{
  // Stop threads while processor state members are still alive.
  oscBridge.reset();
  probeBridge.reset();
}

void GainPluginAudioProcessor::prepareToPlay(double sampleRate, int)
{
  synth.setCurrentPlaybackSampleRate(sampleRate);
  updateMasterEqCoefficients(sampleRate);
  resetMasterEqStates();
  updateTransportSnapshot();
}

void GainPluginAudioProcessor::releaseResources()
{
}

void GainPluginAudioProcessor::resetMasterEqStates()
{
  masterEqLowState = {0.0f, 0.0f};
  masterEqLowMidState = {0.0f, 0.0f};
  masterEqHighMidState = {0.0f, 0.0f};
}

void GainPluginAudioProcessor::updateMasterEqCoefficients(double sampleRate)
{
  const auto sr = static_cast<float>(juce::jmax(8000.0, sampleRate));
  if (std::abs(sr - masterEqCachedSampleRate) < 0.5f)
  {
    return;
  }

  masterEqCachedSampleRate = sr;
  const auto calcAlpha = [sr](float cutoffHz) {
    const auto alpha = std::exp(-(kTwoPi * cutoffHz) / sr);
    return juce::jlimit(0.0f, 0.9999f, alpha);
  };

  masterEqLowAlpha = calcAlpha(180.0f);
  masterEqLowMidAlpha = calcAlpha(1200.0f);
  masterEqHighMidAlpha = calcAlpha(5200.0f);
}

void GainPluginAudioProcessor::processMasterOutputEq(juce::AudioBuffer<float>& buffer)
{
  const auto numChannels = juce::jmin(2, buffer.getNumChannels());
  if (numChannels <= 0)
  {
    return;
  }

  const auto numSamples = buffer.getNumSamples();
  if (numSamples <= 0)
  {
    return;
  }

  const auto low = eqLowParam != nullptr ? juce::jlimit(0.0f, 1.0f, eqLowParam->load()) : 0.5f;
  const auto lowMid = eqLowMidParam != nullptr ? juce::jlimit(0.0f, 1.0f, eqLowMidParam->load()) : 0.5f;
  const auto highMid = eqHighMidParam != nullptr ? juce::jlimit(0.0f, 1.0f, eqHighMidParam->load()) : 0.5f;
  const auto high = eqHighParam != nullptr ? juce::jlimit(0.0f, 1.0f, eqHighParam->load()) : 0.5f;

  // Master output 4-band EQ range: ±18 dB.
  const auto lowGain = juce::Decibels::decibelsToGain((low - 0.5f) * 36.0f);
  const auto lowMidGain = juce::Decibels::decibelsToGain((lowMid - 0.5f) * 36.0f);
  const auto highMidGain = juce::Decibels::decibelsToGain((highMid - 0.5f) * 36.0f);
  const auto highGain = juce::Decibels::decibelsToGain((high - 0.5f) * 36.0f);

  const auto onePole = [](float in, float alpha, float& state) {
    state = ((1.0f - alpha) * in) + (alpha * state);
    return state;
  };

  for (int ch = 0; ch < numChannels; ++ch)
  {
    auto* data = buffer.getWritePointer(ch);
    auto& lowState = masterEqLowState[static_cast<size_t>(ch)];
    auto& lowMidState = masterEqLowMidState[static_cast<size_t>(ch)];
    auto& highMidState = masterEqHighMidState[static_cast<size_t>(ch)];

    for (int i = 0; i < numSamples; ++i)
    {
      const auto in = data[i];
      const auto lowLp = onePole(in, masterEqLowAlpha, lowState);
      const auto lowMidLp = onePole(in, masterEqLowMidAlpha, lowMidState);
      const auto highMidLp = onePole(in, masterEqHighMidAlpha, highMidState);

      const auto lowBand = lowLp;
      const auto lowMidBand = lowMidLp - lowLp;
      const auto highMidBand = highMidLp - lowMidLp;
      const auto highBand = in - highMidLp;

      const auto eqOut =
        (lowBand * lowGain) +
        (lowMidBand * lowMidGain) +
        (highMidBand * highMidGain) +
        (highBand * highGain);

      data[i] = std::tanh(eqOut * 1.06f) * 0.94f;
    }
  }
}

bool GainPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
  {
    return false;
  }

  const auto output = layouts.getMainOutputChannelSet();
  return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void GainPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
  juce::ScopedNoDenormals noDenormals;
  updateTransportSnapshot();
  updateMasterEqCoefficients(getSampleRate());
  buffer.clear();
  synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
  processMasterOutputEq(buffer);
}

void GainPluginAudioProcessor::updateTransportSnapshot()
{
  TransportSnapshot next;
  {
    const juce::ScopedLock lock(stateLock);
    next = transportSnapshot;
  }

  if (auto* hostPlayHead = getPlayHead())
  {
    if (auto position = hostPlayHead->getPosition())
    {
      if (const auto bpm = position->getBpm())
        next.bpm = *bpm;
      if (const auto sig = position->getTimeSignature())
      {
        next.numerator = sig->numerator;
        next.denominator = sig->denominator;
      }
      if (const auto ppq = position->getPpqPosition())
        next.ppqPosition = *ppq;
      if (const auto lastBar = position->getPpqPositionOfLastBarStart())
        next.ppqLastBar = *lastBar;
      if (const auto barCount = position->getBarCount())
        next.barCount = *barCount;
      next.isPlaying = position->getIsPlaying();
      next.isRecording = position->getIsRecording();
    }
  }

  const juce::ScopedLock lock(stateLock);
  transportSnapshot = next;
}

juce::String GainPluginAudioProcessor::buildPayloadPreview(const juce::String& payload, int maxChars)
{
  auto compact = payload.replaceCharacters("\r\n\t", "   ").trim();
  if (compact.isEmpty())
    return "(empty)";
  if (compact.length() <= maxChars)
    return compact;
  return compact.substring(0, juce::jmax(0, maxChars - 3)) + "...";
}

void GainPluginAudioProcessor::markOscIngress(
  const juce::String& address,
  const juce::String& payloadJson,
  const juce::String& senderIp,
  int senderPort)
{
  const juce::ScopedLock lock(stateLock);
  lastOscEndpoint = trimOscPrefix(address);
  lastOscPeer = senderIp.isNotEmpty() ? (senderIp + ":" + juce::String(senderPort)) : "unknown";
  lastGatewayPayloadPreview = buildPayloadPreview(payloadJson, 150);
  lastOscIngressMs = juce::Time::getMillisecondCounterHiRes();
  ++oscIngressCount;
}

void GainPluginAudioProcessor::markExternalGenerationBusy(const juce::String& reason)
{
  const juce::ScopedLock lock(stateLock);
  externalGenerationBusy = true;
  externalBusyReason = reason.trim();
  externalBusyTouchedMs = juce::Time::getMillisecondCounterHiRes();
  ++externalBusySeq;
}

void GainPluginAudioProcessor::clearExternalGenerationBusy()
{
  const juce::ScopedLock lock(stateLock);
  externalGenerationBusy = false;
  externalBusyTouchedMs = juce::Time::getMillisecondCounterHiRes();
  ++externalBusySeq;
}

bool GainPluginAudioProcessor::isExternalGenerationBusy() const
{
  bool busy = false;
  double touchedMs = 0.0;
  {
    const juce::ScopedLock lock(stateLock);
    busy = externalGenerationBusy;
    touchedMs = externalBusyTouchedMs;
  }

  if (!busy)
    return false;
  if (touchedMs <= 0.0)
    return false;

  const auto nowMs = juce::Time::getMillisecondCounterHiRes();
  return (nowMs - touchedMs) <= kExternalBusyTimeoutMs;
}

juce::String GainPluginAudioProcessor::getExternalBusyReason() const
{
  bool busy = false;
  double touchedMs = 0.0;
  juce::String reason;
  {
    const juce::ScopedLock lock(stateLock);
    busy = externalGenerationBusy;
    touchedMs = externalBusyTouchedMs;
    reason = externalBusyReason;
  }

  if (!busy || touchedMs <= 0.0)
    return {};
  const auto nowMs = juce::Time::getMillisecondCounterHiRes();
  if ((nowMs - touchedMs) > kExternalBusyTimeoutMs)
    return {};
  return reason;
}

juce::String GainPluginAudioProcessor::getGatewayStatusReport() const
{
  TransportSnapshot snapshot;
  int track = 1;
  juce::File midiFile;
  juce::String toneType;
  juce::String toneSummary;
  juce::String endpoint;
  juce::String peer;
  juce::String payloadPreview;
  double lastIngressMs = 0.0;
  int64_t ingressCount = 0;
  bool externalBusy = false;
  juce::String externalReason;
  double externalTouchedMs = 0.0;
  int64_t externalSeq = 0;
  int probeCount = 0;
  juce::String activeTag;

  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    track = lastPlacedTrack;
    midiFile = lastPlacedMidiFile;
    toneType = lastToneType;
    toneSummary = lastToneSummary;
    endpoint = lastOscEndpoint;
    peer = lastOscPeer;
    payloadPreview = lastGatewayPayloadPreview;
    lastIngressMs = lastOscIngressMs;
    ingressCount = oscIngressCount;
    externalBusy = externalGenerationBusy;
    externalReason = externalBusyReason;
    externalTouchedMs = externalBusyTouchedMs;
    externalSeq = externalBusySeq;
    probeCount = static_cast<int>(probeStateMap.size());
    activeTag = activeTrackTag;
  }

  // Read tool status
  juce::String toolStatus;
  juce::String toolState;
  int toolProgress = 0;
  juce::String toolHint;
  double toolUpdatedMs = 0.0;
  {
    const juce::ScopedLock lock(stateLock);
    toolStatus = toolStatusMessage;
    toolState = toolStatusState;
    toolProgress = toolStatusProgress;
    toolHint = toolStatusHint;
    toolUpdatedMs = toolStatusUpdatedMs;
  }

  const auto nowMs = juce::Time::getMillisecondCounterHiRes();
  const auto ageSec = lastIngressMs > 0.0 ? (nowMs - lastIngressMs) / 1000.0 : -1.0;
  const auto oscOnline = ageSec >= 0.0 && ageSec <= 12.0;
  const auto oscState = oscOnline ? "CONNECTED" : (ingressCount > 0 ? "IDLE" : "WAITING");
  const auto oscSince = ageSec < 0.0 ? "never" : (ageSec <= 12.0 ? (juce::String(ageSec, 1) + "s ago") : "stale (>12s)");
  const auto ppqIntoBar = juce::jmax(0.0, snapshot.ppqPosition - snapshot.ppqLastBar);
  const auto beatInBar = 1 + static_cast<int>(std::floor(ppqIntoBar));
  const auto payloadText = payloadPreview.isNotEmpty() ? payloadPreview : "(none)";
  const auto endpointText = endpoint.isNotEmpty() ? endpoint : "(none)";
  const auto peerText = peer.isNotEmpty() ? peer : "(none)";
  const auto midiName = midiFile.existsAsFile() ? midiFile.getFileName() : "(none)";
  const auto toneText = toneType.isNotEmpty() ? toneType : "(none)";
  const auto toneSummaryText =
    toneSummary.isNotEmpty() ? buildPayloadPreview(toneSummary, 64) : "(none)";
  const auto extAgeSec = externalTouchedMs > 0.0 ? (nowMs - externalTouchedMs) / 1000.0 : -1.0;
  const auto extBusyEffective = externalBusy && extAgeSec >= 0.0 && extAgeSec <= (kExternalBusyTimeoutMs / 1000.0);
  const auto extState = extBusyEffective ? "BUSY" : "IDLE";
  const auto extSince =
    extAgeSec < 0.0 ? "never" : (extAgeSec <= (kExternalBusyTimeoutMs / 1000.0) ? (juce::String(extAgeSec, 1) + "s ago") : "stale");
  const auto extReason = externalReason.isNotEmpty() ? externalReason : "(none)";

  // Tool status formatting
  const auto toolAgeSec = toolUpdatedMs > 0.0 ? (nowMs - toolUpdatedMs) / 1000.0 : -1.0;
  const auto toolAgeText = toolAgeSec < 0.0 ? "never" : (juce::String(toolAgeSec, 1) + "s ago");
  const auto toolStateColor = [=]() -> const char* {
    if (toolState == "WaitingForInput") return "WAIT";
    if (toolState == "GettingInfo") return "INFO";
    if (toolState == "Generating") return "GEN";
    if (toolState == "Placing") return "PLACE";
    if (toolState == "Completed") return "OK";
    if (toolState == "Failed") return "FAIL";
    return "IDLE";
  }();

  juce::String report;

  // Tool operation status (most prominent for user feedback)
  if (toolStatus.isNotEmpty() || toolState.isNotEmpty())
  {
    report << "[TOOL:" << toolStateColor << "] ";
    if (toolProgress > 0)
      report << toolProgress << "% ";
    report << (toolStatus.isNotEmpty() ? toolStatus : "(processing)") << "\n";
    if (toolHint.isNotEmpty())
      report << "  -> " << toolHint << "\n";
    report << "  updated " << toolAgeText << "\n";
  }

  report << "OSC " << oscState << " | peer " << peerText << " | packets " << juce::String(ingressCount)
         << " | last " << oscSince << " | endpoint " << endpointText << "\n";
  report << "External task: " << extState << " | seq " << juce::String(externalSeq)
         << " | last " << extSince << " | reason " << buildPayloadPreview(extReason, 64) << "\n";
  report << "Probe: " << juce::String(probeCount) << " online | active_track_tag "
         << (activeTag.isNotEmpty() ? activeTag : "(none)") << "\n";
  report << "Gateway payload: " << payloadText << "\n";
  report << "Transport: " << juce::String(snapshot.bpm, 2) << " BPM  "
         << juce::String(snapshot.numerator) << "/" << juce::String(snapshot.denominator)
         << "  Bar " << juce::String(static_cast<int>(snapshot.barCount + 1))
         << " Beat " << juce::String(beatInBar)
         << "  PPQ " << juce::String(snapshot.ppqPosition, 2) << "\n";
  report << "Track " << juce::String(juce::jmax(1, track)) << " | Last MIDI " << midiName
         << " | Tone " << toneText << " | Summary " << toneSummaryText;
  return report;
}

bool GainPluginAudioProcessor::consumeClipboardNotice(juce::String& outMessage)
{
  const juce::ScopedLock lock(stateLock);
  if (!hasPendingClipboardNotice)
    return false;
  outMessage = pendingClipboardNotice;
  pendingClipboardNotice.clear();
  hasPendingClipboardNotice = false;
  return true;
}

bool GainPluginAudioProcessor::consumePlacedMidiUpdate(juce::File& outPath)
{
  const juce::ScopedLock lock(stateLock);
  if (midiPlacementSeq == midiPlacementConsumedSeq)
    return false;
  midiPlacementConsumedSeq = midiPlacementSeq;
  outPath = lastPlacedMidiFile;
  return outPath.existsAsFile();
}

GainPluginAudioProcessor::ToolStatusInfo GainPluginAudioProcessor::getToolStatusInfo() const
{
  ToolStatusInfo info;
  {
    const juce::ScopedLock lock(stateLock);
    info.toolName = toolStatusToolName;
    info.state = toolStatusState;
    info.message = toolStatusMessage;
    info.hint = toolStatusHint;
    info.progress = toolStatusProgress;
    info.updatedMs = toolStatusUpdatedMs;
  }
  return info;
}

juce::String GainPluginAudioProcessor::getProbeMidiDetailsReport() const
{
  juce::String report;
  double nowMs = juce::Time::getMillisecondCounterHiRes();

  std::map<int, ProbeState> probes;
  {
    const juce::ScopedLock lock(stateLock);
    probes = probeStateMap;
  }

  if (probes.empty())
  {
    return "Probe: No probes connected";
  }

  int probeCount = 0;
  for (const auto& kv : probes)
  {
    const auto& st = kv.second;
    const auto ageMs = nowMs - st.lastSeenLocalMs;
    if (st.lastSeenLocalMs <= 0.0 || ageMs > kProbeTtlMs)
      continue;

    ++probeCount;

    juce::String midiInfo;
    if (st.midiDetails.noteCount > 0)
    {
      midiInfo = juce::String(st.midiDetails.noteCount) + " notes | " +
                 juce::String(st.midiDetails.windowStartMs, 0) + "-" +
                 juce::String(st.midiDetails.windowEndMs, 0) + "ms";
    }
    else if (st.midiDetails.notesJson.isNotEmpty())
    {
      midiInfo = st.midiDetails.notesJson.substring(0, juce::jmin(60, st.midiDetails.notesJson.length())) + "...";
    }
    else
    {
      midiInfo = "Listening...";
    }

    report << "T" << juce::String(st.trackNumber) << " [" << st.category << "] " << midiInfo;
  }

  if (report.isEmpty())
    return "Probe: No active probes";

  return "Probe: " + report;
}

juce::String GainPluginAudioProcessor::getTransportSelection() const
{
  TransportSnapshot snapshot;
  bool hasSel = false;
  double startPpq = 0.0, endPpq = 0.0;
  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    hasSel = hasSelection;
    startPpq = selectionStartPpq;
    endPpq = selectionEndPpq;
  }

  if (!hasSel)
  {
    return "No selection - Please select a time range in your DAW (loop range or work selection)";
  }

  const auto ppqPerBeat = 4.0;  // Assuming 4/4 time
  const auto startBar = static_cast<int>(startPpq / (snapshot.numerator * ppqPerBeat / snapshot.denominator));
  const auto endBar = static_cast<int>(endPpq / (snapshot.numerator * ppqPerBeat / snapshot.denominator));
  const auto beats = (endPpq - startPpq) / ppqPerBeat;

  return juce::String("Selection: Bar ") + juce::String(startBar + 1) + "-" + juce::String(endBar + 1) +
         " (" + juce::String(beats, 1) + " beats, " + juce::String(startPpq, 1) + "-" + juce::String(endPpq, 1) + " PPQ)";
}

void GainPluginAudioProcessor::requestProbeMidiDetails()
{
  TransportSnapshot snapshot;
  bool hasSel = false;
  double startPpq = 0.0, endPpq = 0.0;
  double bpm = 120.0;
  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    hasSel = hasSelection;
    startPpq = selectionStartPpq;
    endPpq = selectionEndPpq;
    bpm = snapshot.bpm;
  }

  if (!hasSel)
  {
    // 没有选区，通知用户
    const juce::ScopedLock lock(stateLock);
    externalBusyReason = "No selection - Please select a time range in DAW first";
    return;
  }

  // PPQ 转换为毫秒：ms = PPQ * (60000 / BPM) / 4
  const auto msPerPpq = (60000.0 / bpm) / 4.0;
  const auto windowStartMs = startPpq * msPerPpq;
  const auto windowEndMs = endPpq * msPerPpq;

  // 清除之前的结果
  {
    const juce::ScopedLock lock(probeResultsLock);
    probeMidiResults.clear();
  }

  // UDP 广播：Probes 会定期发送状态过来，无需主动请求
  // 这里只检查 bridge 是否就绪
  if (probeBridge && probeBridge->isReady())
  {
    // Probes will send MIDI details on their next broadcast
  }
}


juce::String GainPluginAudioProcessor::buildJsonReply(const juce::var& value)
{
  return juce::JSON::toString(value, false);
}

juce::String GainPluginAudioProcessor::handlePingRequest() const
{
  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("ok", true);
  root->setProperty("service", "xiwuclaw_vst");
  root->setProperty("plugin", JucePlugin_Name);
  root->setProperty("osc_port", kGatewayOscPort);
  return buildJsonReply(juce::var(root.get()));
}

juce::String GainPluginAudioProcessor::handleTransportRequest() const
{
  TransportSnapshot snapshot;
  int track = 1;
  juce::File midiFile;
  double placedPpq = 0.0;
  juce::String toneType;
  juce::String toneSummary;
  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    track = lastPlacedTrack;
    midiFile = lastPlacedMidiFile;
    placedPpq = lastPlacedPpq;
    toneType = lastToneType;
    toneSummary = lastToneSummary;
  }

  const auto ppqIntoBar = juce::jmax(0.0, snapshot.ppqPosition - snapshot.ppqLastBar);
  const auto beatInBar = 1 + static_cast<int>(std::floor(ppqIntoBar));

  juce::DynamicObject::Ptr timeSig = new juce::DynamicObject();
  timeSig->setProperty("numerator", snapshot.numerator);
  timeSig->setProperty("denominator", snapshot.denominator);

  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("ok", true);
  root->setProperty("bpm", snapshot.bpm);
  root->setProperty("beats_per_bar", snapshot.numerator);
  root->setProperty("beat_unit", snapshot.denominator);
  root->setProperty("time_signature", juce::var(timeSig.get()));
  root->setProperty("playhead_beats", snapshot.ppqPosition);
  root->setProperty("playhead_bar", static_cast<int>(snapshot.barCount + 1));
  root->setProperty("playhead_beat", beatInBar);
  root->setProperty("track_index", track);
  root->setProperty("is_playing", snapshot.isPlaying);
  root->setProperty("is_recording", snapshot.isRecording);
  root->setProperty("last_placed_ppq", placedPpq);
  root->setProperty("last_midi_path", midiFile.getFullPathName());
  root->setProperty("last_tone_type", toneType);
  root->setProperty("last_tone_summary", toneSummary);
  return buildJsonReply(juce::var(root.get()));
}

void GainPluginAudioProcessor::pruneExpiredProbes(const double nowMs)
{
  const juce::ScopedLock lock(stateLock);
  for (auto it = probeStateMap.begin(); it != probeStateMap.end();)
  {
    const auto age = nowMs - it->second.lastSeenLocalMs;
    if (it->second.lastSeenLocalMs <= 0.0 || age > kProbeTtlMs)
      it = probeStateMap.erase(it);
    else
      ++it;
  }
}

void GainPluginAudioProcessor::ingestProbeStateMessage(const juce::var& payload)
{
  if (auto* obj = payload.getDynamicObject())
  {
    // 检查是否是 announce 命令
    const auto cmd = readJsonString(payload, "cmd", {});

    // 读取 track_number（主要标识符）
    const int trackNum = readJsonInt(payload, "track_number", 1);
    const auto category = readJsonString(payload, "category", {});
    const auto probeId = readJsonString(payload, "probe_id", {});

    // announce: 注册 probe
    if (cmd == "announce")
    {
      {
        const juce::ScopedLock lock(stateLock);
        if (probeStateMap.find(trackNum) == probeStateMap.end())
        {
          ProbeState state;
          state.trackNumber = trackNum;
          state.category = category;
          state.probeId = probeId;
          state.lastSeenLocalMs = juce::Time::getMillisecondCounterHiRes();
          probeStateMap[trackNum] = state;
        }
        else
        {
          auto& state = probeStateMap[trackNum];
          state.probeId = probeId;
          state.category = category;
          state.lastSeenLocalMs = juce::Time::getMillisecondCounterHiRes();
        }
      }
      return;
    }

    // 检查是否是 MIDI 详情响应
    if (obj->hasProperty("midi_details"))
    {
      const auto midiDetails = obj->getProperty("midi_details");
      if (!midiDetails.isVoid())
      {
        // 更新 state
        {
          const juce::ScopedLock lock(stateLock);
          if (probeStateMap.find(trackNum) != probeStateMap.end())
          {
            auto& state = probeStateMap[trackNum];
            state.midiDetails.notesJson = juce::JSON::toString(midiDetails);
            state.midiDetails.noteCount = readJsonInt(midiDetails, "note_count", 0);
            state.midiDetails.windowStartMs = readJsonDouble(midiDetails, "window_start_ms", 0.0);
            state.midiDetails.windowEndMs = readJsonDouble(midiDetails, "window_end_ms", 0.0);
            state.midiDetails.lastUpdatedMs = juce::Time::getMillisecondCounterHiRes();
          }
        }

        // 创建可拖拽的 MIDI 结果项
        const auto outputDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("XiwuCLAW_ProbeMidi");
        outputDir.createDirectory();
        const auto midiFile = midiTracker_exportMidiFile(outputDir, category + "_T" + juce::String(trackNum),
            midiDetails,
            readJsonDouble(midiDetails, "window_start_ms", 0.0),
            readJsonDouble(midiDetails, "window_end_ms", 0.0));

        {
          const juce::ScopedLock lock(probeResultsLock);
          ProbeMidiResult result;
          result.trackNumber = trackNum;
          result.category = category;
          result.probeId = probeId;
          result.midiFile = midiFile;
          result.noteCount = readJsonInt(midiDetails, "note_count", 0);
          result.windowStartMs = readJsonDouble(midiDetails, "window_start_ms", 0.0);
          result.windowEndMs = readJsonDouble(midiDetails, "window_end_ms", 0.0);
          result.ready = midiFile.existsAsFile();

          // 替换同一 trackNumber 的旧结果，避免无限累积
          bool found = false;
          for (size_t i = 0; i < probeMidiResults.size(); ++i)
          {
            if (probeMidiResults[i].trackNumber == trackNum)
            {
              probeMidiResults[i].midiFile.deleteFile();
              probeMidiResults[i] = result;
              found = true;
              break;
            }
          }
          if (!found)
            probeMidiResults.push_back(result);
        }
      }
      return;
    }

    // 常规状态更新
    ProbeState state;
    state.trackNumber = trackNum;
    state.category = category;
    state.probeId = probeId;
    state.timestampMs = static_cast<int64_t>(readJsonDouble(payload, "timestamp_ms", 0.0));
    state.midiActivity = readJsonInt(payload, "midi_activity", 0);
    state.noteDensity = readJsonDouble(payload, "note_density", 0.0);
    state.audioPeak = readJsonDouble(payload, "audio_peak", 0.0);
    state.lastSeenLocalMs = juce::Time::getMillisecondCounterHiRes();

    if (obj->hasProperty("transport_hint"))
    {
      const auto th = obj->getProperty("transport_hint");
      state.transport.bar = readJsonInt(th, "bar", 0);
      state.transport.beat = readJsonInt(th, "beat", 0);
      state.transport.ppq = readJsonDouble(th, "ppq", 0.0);
      state.transport.valid = true;
    }

    {
      const juce::ScopedLock lock(stateLock);
      probeStateMap[trackNum] = state;
    }
  }
}

// 辅助函数：从 JSON 导出 MIDI 文件
juce::File GainPluginAudioProcessor::midiTracker_exportMidiFile(
    const juce::File& outputDir, const juce::String& trackTag,
    const juce::var& midiDetails,
    double windowStartMs, double windowEndMs)
{
  const auto fileName = trackTag + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".mid";
  const auto midiFile = outputDir.getChildFile(fileName);

  juce::MidiMessageSequence sequence;
  const auto firstTime = windowStartMs;

  if (auto* details = midiDetails.getDynamicObject())
  {
    if (auto* notesArray = details->getProperty("notes").getArray())
    {
      for (int i = 0; i < notesArray->size(); ++i)
      {
        if (auto* noteObj = notesArray->getReference(i).getDynamicObject())
        {
          const auto timeMs = static_cast<double>(noteObj->getProperty("time_ms"));
          const auto note = static_cast<int>(noteObj->getProperty("note"));
          const auto velocity = static_cast<float>(static_cast<double>(noteObj->getProperty("velocity")));
          const auto isOn = static_cast<bool>(noteObj->getProperty("on"));

          const auto deltaMs = timeMs;  // 已经是相对时间
          const auto deltaTicks = static_cast<int>(deltaMs * 0.48); // 120 BPM, 480 ticks/beat

          if (isOn)
          {
            sequence.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<uint8>(velocity * 127)), deltaTicks);
          }
          else
          {
            sequence.addEvent(juce::MidiMessage::noteOff(1, note), deltaTicks);
          }
        }
      }
    }
  }

  // 即使没有音符也导出空 MIDI 文件（规范格式）
  sequence.updateMatchedPairs();
  // Add time signature (4/4) and tempo (120 BPM = 500000 microseconds per beat)
  sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
  sequence.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0); // 120 BPM

  juce::MidiFile midi;
  midi.addTrack(sequence);
  midi.setTicksPerQuarterNote(480);

  juce::MemoryOutputStream block;
  if (midi.writeTo(block))
  {
    if (midiFile.replaceWithData(block.getData(), block.getDataSize()))
    {
      return midiFile;
    }
  }
  return juce::File();
}

juce::String GainPluginAudioProcessor::handleActiveTrackSnapshotRequest() const
{
  const auto nowMs = juce::Time::getMillisecondCounterHiRes();

  TransportSnapshot snapshot;
  std::map<int, ProbeState> probes;
  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    probes = probeStateMap;
  }

  // Prune on demand.
  const_cast<GainPluginAudioProcessor*>(this)->pruneExpiredProbes(nowMs);

  juce::DynamicObject::Ptr timeSig = new juce::DynamicObject();
  timeSig->setProperty("numerator", snapshot.numerator);
  timeSig->setProperty("denominator", snapshot.denominator);

  juce::Array<juce::var> probeArr;
  for (const auto& kv : probes)
  {
    const auto& st = kv.second;
    const auto ageMs = nowMs - st.lastSeenLocalMs;
    if (st.lastSeenLocalMs <= 0.0 || ageMs > kProbeTtlMs)
      continue;

    juce::DynamicObject::Ptr p = new juce::DynamicObject();
    p->setProperty("track_number", st.trackNumber);
    p->setProperty("category", st.category);
    p->setProperty("probe_id", st.probeId);
    p->setProperty("timestamp_ms", st.timestampMs);
    p->setProperty("midi_activity", st.midiActivity);
    p->setProperty("note_density", st.noteDensity);
    p->setProperty("audio_peak", st.audioPeak);
    p->setProperty("age_ms", ageMs);
    if (st.transport.valid)
    {
      juce::DynamicObject::Ptr th = new juce::DynamicObject();
      th->setProperty("bar", st.transport.bar);
      th->setProperty("beat", st.transport.beat);
      th->setProperty("ppq", st.transport.ppq);
      p->setProperty("transport_hint", juce::var(th.get()));
    }
    probeArr.add(juce::var(p.get()));
  }

  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  root->setProperty("ok", true);
  root->setProperty("probe_online_count", probeArr.size());
  root->setProperty("probes", juce::var(probeArr));
  root->setProperty("bpm", snapshot.bpm);
  root->setProperty("beats_per_bar", snapshot.numerator);
  root->setProperty("beat_unit", snapshot.denominator);
  root->setProperty("time_signature", juce::var(timeSig.get()));
  root->setProperty("playhead_beats", snapshot.ppqPosition);
  root->setProperty("playhead_bar", static_cast<int>(snapshot.barCount + 1));
  const auto ppqIntoBar = juce::jmax(0.0, snapshot.ppqPosition - snapshot.ppqLastBar);
  const auto beatInBar = 1 + static_cast<int>(std::floor(ppqIntoBar));
  root->setProperty("playhead_beat", beatInBar);
  return buildJsonReply(juce::var(root.get()));
}

void GainPluginAudioProcessor::applyFloatParamIfPresent(
  const juce::var& source,
  const juce::Identifier& key,
  std::atomic<float>* targetParam)
{
  if (targetParam == nullptr)
    return;
  if (auto* obj = source.getDynamicObject())
  {
    if (!obj->hasProperty(key))
      return;
    const auto value = static_cast<float>(obj->getProperty(key));
    targetParam->store(juce::jlimit(0.0f, 1.0f, value));
  }
}

int GainPluginAudioProcessor::chooseToneIndexForType(const juce::String& toneType) const
{
  const auto query = toneType.trim().toLowerCase();
  if (query.isEmpty())
    return 0;

  const auto& names = getToneNames();
  for (int i = 0; i < names.size(); ++i)
  {
    if (names[i].toLowerCase().contains(query))
      return i;
  }
  if (query.contains("bass"))
    return juce::jmin(1, names.size() - 1);
  if (query.contains("lead"))
    return juce::jmin(2, names.size() - 1);
  if (query.contains("pad"))
    return juce::jmin(3, names.size() - 1);
  return 0;
}

juce::String GainPluginAudioProcessor::handleToneLoadJsonRequest(const juce::var& payload)
{
  clearExternalGenerationBusy();

  if (auto* root = payload.getDynamicObject())
  {
    const auto tone = root->getProperty("tone");
    const auto toneType = readJsonString(tone, "tone_type", "custom");
    const auto summary = readJsonString(tone, "summary", {});
    const auto eq4 = tone.getProperty("eq4", juce::var());

    applyFloatParamIfPresent(eq4, "low", eqLowParam);
    applyFloatParamIfPresent(eq4, "low_mid", eqLowMidParam);
    applyFloatParamIfPresent(eq4, "high_mid", eqHighMidParam);
    applyFloatParamIfPresent(eq4, "high", eqHighParam);

    const auto toneIndex = chooseToneIndexForType(toneType);
    if (toneModeParam != nullptr)
      toneModeParam->store(static_cast<float>(toneIndex));

    {
      const juce::ScopedLock lock(stateLock);
      lastToneType = toneType;
      lastToneSummary = summary;
    }

    juce::DynamicObject::Ptr reply = new juce::DynamicObject();
    reply->setProperty("ok", true);
    reply->setProperty("message", "tone json loaded into XiwuCLAW");
    reply->setProperty("tone_type", toneType);
    reply->setProperty("tone_index", toneIndex);
    return buildJsonReply(juce::var(reply.get()));
  }

  juce::DynamicObject::Ptr fail = new juce::DynamicObject();
  fail->setProperty("ok", false);
  fail->setProperty("error", "invalid tone payload");
  return buildJsonReply(juce::var(fail.get()));
}

juce::String GainPluginAudioProcessor::handleBusySetRequest(const juce::var& payload)
{
  auto busy = true;
  auto reason = juce::String("external");
  if (auto* root = payload.getDynamicObject())
  {
    if (root->hasProperty("busy"))
      busy = static_cast<bool>(root->getProperty("busy"));
    const auto mode = readJsonString(payload, "mode", {});
    reason = readJsonString(payload, "reason", reason);
    if (mode.isNotEmpty())
      reason = mode;
  }

  if (busy)
    markExternalGenerationBusy(reason);
  else
    clearExternalGenerationBusy();

  juce::DynamicObject::Ptr reply = new juce::DynamicObject();
  reply->setProperty("ok", true);
  reply->setProperty("busy", busy);
  reply->setProperty("reason", reason);
  return buildJsonReply(juce::var(reply.get()));
}

juce::String GainPluginAudioProcessor::handleToolStatusRequest(const juce::var& payload)
{
  const auto nowMs = juce::Time::getMillisecondCounterHiRes();

  {
    const juce::ScopedLock lock(stateLock);
    toolStatusMessage = readJsonString(payload, "message", {});
    toolStatusToolName = readJsonString(payload, "tool_name", {});
    toolStatusState = readJsonString(payload, "state", {});
    toolStatusHint = readJsonString(payload, "user_action_hint", {});
    toolStatusProgress = static_cast<int>(readJsonDouble(payload, "progress", 0.0));
    toolStatusUpdatedMs = nowMs;
  }

  juce::DynamicObject::Ptr reply = new juce::DynamicObject();
  reply->setProperty("ok", true);
  reply->setProperty("received", true);
  return buildJsonReply(juce::var(reply.get()));
}

juce::String GainPluginAudioProcessor::handleMidiPlaceRequest(const juce::var& payload)
{
  clearExternalGenerationBusy();

  const auto midiPath = readJsonString(payload, "midi_path");
  const auto trackIndex = readJsonInt(payload, "track_index", 1);
  const auto playheadPpq = readJsonDouble(payload, "playhead_beats", 0.0);
  const auto playheadBar = readJsonInt(payload, "playhead_bar", 1);
  const auto playheadBeat = readJsonInt(payload, "playhead_beat", 1);

  auto midiFile = juce::File(midiPath);
  auto exists = midiFile.existsAsFile();
  int noteCount = 0;
  int tracks = 0;
  if (exists)
  {
    juce::FileInputStream in(midiFile);
    juce::MidiFile parsed;
    if (parsed.readFrom(in))
    {
      tracks = parsed.getNumTracks();
      for (int t = 0; t < tracks; ++t)
      {
        if (auto* seq = parsed.getTrack(t))
        {
          for (int e = 0; e < seq->getNumEvents(); ++e)
          {
            if (const auto* event = seq->getEventPointer(e))
            {
              if (event->message.isNoteOn())
                ++noteCount;
            }
          }
        }
      }
    }
  }

  {
    const juce::ScopedLock lock(stateLock);
    lastPlacedMidiFile = midiFile;
    lastPlacedTrack = juce::jmax(1, trackIndex);
    lastPlacedPpq = playheadPpq;
    ++midiPlacementSeq;
    pendingClipboardNotice = makeClipboardToastText();
    hasPendingClipboardNotice = true;
  }

  juce::DynamicObject::Ptr reply = new juce::DynamicObject();
  reply->setProperty("ok", true);
  reply->setProperty(
    "message",
    "plugin received midi placement request, but host arrangement clip insertion is not supported in this VST path");
  reply->setProperty("midi_path", midiPath);
  reply->setProperty("midi_exists", exists);
  reply->setProperty("inserted", false);
  reply->setProperty("host_insert_supported", false);
  reply->setProperty("track_index", juce::jmax(1, trackIndex));
  reply->setProperty("playhead_bar", playheadBar);
  reply->setProperty("playhead_beat", playheadBeat);
  reply->setProperty("note_count", noteCount);
  reply->setProperty("midi_tracks", tracks);
  return buildJsonReply(juce::var(reply.get()));
}

// ============================================================================
// MIDI Collection Command Handlers
// ============================================================================

juce::String GainPluginAudioProcessor::handleMidiCollectFullRequest(const juce::var& payload)
{
  // 全量收集：请求所有 Probe 上报当前全部 MIDI 数据
  // Payload: { "action": "midi/collect/full", "output_dir": "/path/to/output" }
  const auto outputDirPath = readJsonString(payload, "output_dir", juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName());
  const auto outputDir = juce::File(outputDirPath);

  if (!outputDir.isDirectory())
  {
    outputDir.createDirectory();
    if (!outputDir.isDirectory())
    {
      juce::DynamicObject::Ptr fail = new juce::DynamicObject();
      fail->setProperty("ok", false);
      fail->setProperty("error", "Cannot create output directory: " + outputDirPath);
      return buildJsonReply(juce::var(fail.get()));
    }
  }

  // 向所有 Probe 发送全量收集请求（使用最大时间窗口）
  // UDP 广播：无法主动请求，Probes 会定期发送状态
  if (probeBridge && probeBridge->isReady())
  {
    // Probes will handle full collection on their next broadcast
  }

  juce::DynamicObject::Ptr reply = new juce::DynamicObject();
  reply->setProperty("ok", true);
  reply->setProperty("message", "Full MIDI collection initiated");
  reply->setProperty("output_dir", outputDirPath);

  // 返回当前在线 Probe 数量
  int probeCount = 0;
  double nowMs = juce::Time::getMillisecondCounterHiRes();
  {
    const juce::ScopedLock lock(stateLock);
    for (const auto& kv : probeStateMap)
    {
      const auto& st = kv.second;
      const auto ageMs = nowMs - st.lastSeenLocalMs;
      if (st.lastSeenLocalMs > 0.0 && ageMs <= kProbeTtlMs)
        ++probeCount;
    }
  }
  reply->setProperty("probe_count", probeCount);

  return buildJsonReply(juce::var(reply.get()));
}

juce::String GainPluginAudioProcessor::handleMidiCollectSelectionRequest(const juce::var& payload)
{
  // 选区收集：只收集用户选定时间范围内的 MIDI 数据
  // Payload: { "action": "midi/collect/selection", "output_dir": "/path/to/output" }
  const auto outputDirPath = readJsonString(payload, "output_dir", juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName());

  // 检查是否有选区
  TransportSnapshot snapshot;
  bool hasSel = false;
  double startPpq = 0.0, endPpq = 0.0;
  {
    const juce::ScopedLock lock(stateLock);
    snapshot = transportSnapshot;
    hasSel = hasSelection;
    startPpq = selectionStartPpq;
    endPpq = selectionEndPpq;
  }

  if (!hasSel)
  {
    juce::DynamicObject::Ptr fail = new juce::DynamicObject();
    fail->setProperty("ok", false);
    fail->setProperty("error", "No selection - Please select a time range in DAW first");
    return buildJsonReply(juce::var(fail.get()));
  }

  const auto outputDir = juce::File(outputDirPath);
  if (!outputDir.isDirectory())
  {
    outputDir.createDirectory();
    if (!outputDir.isDirectory())
    {
      juce::DynamicObject::Ptr fail = new juce::DynamicObject();
      fail->setProperty("ok", false);
      fail->setProperty("error", "Cannot create output directory: " + outputDirPath);
      return buildJsonReply(juce::var(fail.get()));
    }
  }

  // 选区有效，开始收集
  requestProbeMidiDetails();

  juce::DynamicObject::Ptr reply = new juce::DynamicObject();
  reply->setProperty("ok", true);
  reply->setProperty("message", "Selection MIDI collection initiated");
  reply->setProperty("output_dir", outputDirPath);
  reply->setProperty("selection_start_ppq", startPpq);
  reply->setProperty("selection_end_ppq", endPpq);
  reply->setProperty("bpm", snapshot.bpm);

  // 计算选区时长（毫秒）
  const auto msPerPpq = (60000.0 / snapshot.bpm) / 4.0;
  reply->setProperty("selection_duration_ms", (endPpq - startPpq) * msPerPpq);

  return buildJsonReply(juce::var(reply.get()));
}

juce::String GainPluginAudioProcessor::handleOscCommand(
  const juce::String& address,
  const juce::String& payloadJson)
{
  const auto endpoint = trimOscPrefix(address);
  const auto payload = payloadJson.isEmpty() ? juce::var() : juce::JSON::parse(payloadJson);

  if (endpoint == "/ping")
    return handlePingRequest();
  if (endpoint == "/transport/get")
  {
    markExternalGenerationBusy(readJsonString(payload, "action", "transport/get"));
    return handleTransportRequest();
  }
  if (endpoint == "/track/active/snapshot/get")
  {
    markExternalGenerationBusy(readJsonString(payload, "action", "track/active/snapshot/get"));
    return handleActiveTrackSnapshotRequest();
  }
  if (endpoint == "/midi/place")
    return handleMidiPlaceRequest(payload);
  if (endpoint == "/tone/load_json")
    return handleToneLoadJsonRequest(payload);
  if (endpoint == "/task/busy")
    return handleBusySetRequest(payload);
  if (endpoint == "/status")
    return handleToolStatusRequest(payload);
  if (endpoint == "/midi/collect/full")
  {
    markExternalGenerationBusy(readJsonString(payload, "action", "midi/collect/full"));
    return handleMidiCollectFullRequest(payload);
  }
  if (endpoint == "/midi/collect/selection")
  {
    markExternalGenerationBusy(readJsonString(payload, "action", "midi/collect/selection"));
    return handleMidiCollectSelectionRequest(payload);
  }

  juce::DynamicObject::Ptr fail = new juce::DynamicObject();
  fail->setProperty("ok", false);
  fail->setProperty("error", "unsupported endpoint");
  fail->setProperty("endpoint", endpoint);
  return buildJsonReply(juce::var(fail.get()));
}

bool GainPluginAudioProcessor::readOscPaddedString(
  const uint8_t* data,
  size_t size,
  size_t& offset,
  juce::String& outText)
{
  if (offset >= size)
    return false;
  size_t end = offset;
  while (end < size && data[end] != 0)
    ++end;
  if (end >= size)
    return false;

  outText = juce::String::fromUTF8(reinterpret_cast<const char*>(data + offset), static_cast<int>(end - offset));
  offset = end + 1;
  while ((offset % 4) != 0)
    ++offset;
  return offset <= size;
}

bool GainPluginAudioProcessor::decodeOscAddressAndStringArg(
  const void* data,
  size_t size,
  juce::String& outAddress,
  juce::String& outStringArg)
{
  auto* bytes = static_cast<const uint8_t*>(data);
  size_t offset = 0;
  juce::String tag;
  if (!readOscPaddedString(bytes, size, offset, outAddress))
    return false;
  if (!readOscPaddedString(bytes, size, offset, tag))
    return false;
  if (!tag.startsWith(","))
    return false;

  const auto format = tag.substring(1);
  for (int i = 0; i < format.length(); ++i)
  {
    const auto c = format[i];
    if (c == 's')
    {
      juce::String arg;
      if (!readOscPaddedString(bytes, size, offset, arg))
        return false;
      outStringArg = arg;
      return true;
    }
    if (c == 'i' || c == 'f')
      offset += 4;
    if (offset > size)
      return false;
  }
  return false;
}

const juce::StringArray& GainPluginAudioProcessor::getToneNames()
{
  static const auto names = [] {
    juce::StringArray result;
    for (const auto& tone : getFactoryTones())
    {
      result.add(tone.name);
    }
    return result;
  }();

  return names;
}

juce::String GainPluginAudioProcessor::getToneLibraryReport()
{
  juce::StringArray lines;
  const auto& tones = getFactoryTones();
  lines.add("Xiwu Tone Library");
  lines.add("-----------------");
  lines.add("Total presets: " + juce::String(static_cast<int>(tones.size())));
  lines.add({});

  for (size_t i = 0; i < tones.size(); ++i)
  {
    const auto& t = tones[i];
    lines.add(juce::String(static_cast<int>(i + 1)) + ". " + t.name + "  [" + t.id + "]");
    lines.add("    source: " + t.source);
  }

  return lines.joinIntoString("\n");
}

juce::AudioProcessorValueTreeState::ParameterLayout GainPluginAudioProcessor::createParameterLayout()
{
  std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

  params.push_back(std::make_unique<juce::AudioParameterChoice>(
    juce::ParameterID{"tone_mode", 1},
    "Tone Mode",
    getToneNames(),
    0));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"master_gain", 1},
    "Master Gain",
    juce::NormalisableRange<float>{0.0f, 1.2f, 0.001f},
    0.82f,
    juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float value, int) {
      return juce::String(value, 2) + "x";
    })));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"eq_low", 1}, "EQ Low", juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"eq_low_mid", 1}, "EQ Low-Mid", juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"eq_high_mid", 1}, "EQ High-Mid", juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.5f));

  params.push_back(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"eq_high", 1}, "EQ High", juce::NormalisableRange<float>{0.0f, 1.0f, 0.001f}, 0.5f));

  return {params.begin(), params.end()};
}

void GainPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
  const auto state = parameters.copyState();
  if (auto xml = state.createXml())
  {
    copyXmlToBinary(*xml, destData);
  }
}

void GainPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
  {
    if (xmlState->hasTagName(parameters.state.getType()))
    {
      parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
  }
}

juce::AudioProcessorEditor* GainPluginAudioProcessor::createEditor()
{
  return new GainPluginAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
  return new GainPluginAudioProcessor();
}
