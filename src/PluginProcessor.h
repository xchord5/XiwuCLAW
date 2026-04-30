#pragma once

#include <JuceHeader.h>
#include <array>
#include <map>

class GainPluginAudioProcessor final : public juce::AudioProcessor
{
public:
  GainPluginAudioProcessor();
  ~GainPluginAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.1; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  static const juce::StringArray& getToneNames();
  static juce::String getToneLibraryReport();
  juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }
  juce::String getGatewayStatusReport() const;
  juce::String getProbeMidiDetailsReport() const;

  // Tool status for UI display
  struct ToolStatusInfo {
    juce::String toolName;
    juce::String state;
    juce::String message;
    juce::String hint;
    int progress = 0;
    double updatedMs = 0.0;
  };
  ToolStatusInfo getToolStatusInfo() const;
  juce::String getTransportSelection() const;  // 获取播放选区信息
  void requestProbeMidiDetails();  // 请求所有 Probe 返回 MIDI 详情
  bool consumeClipboardNotice(juce::String& outMessage);
  bool isExternalGenerationBusy() const;
  juce::String getExternalBusyReason() const;
  bool consumePlacedMidiUpdate(juce::File& outPath);

private:
  struct TransportSnapshot
  {
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    double ppqPosition = 0.0;
    double ppqLastBar = 0.0;
    int64_t barCount = 0;
    bool isPlaying = false;
    bool isRecording = false;
  };

  class OscGatewayBridge;
  class ProbeUdpBridge;

  struct ProbeTransportHint
  {
    int bar = 0;
    int beat = 0;
    double ppq = 0.0;
    bool valid = false;
  };

  struct ProbeMidiDetails
  {
    int noteCount = 0;
    double windowStartMs = 0.0;
    double windowEndMs = 0.0;
    juce::String notesJson;
    double lastUpdatedMs = 0.0;
  };

  struct ProbeMidiResult
  {
    int trackNumber = 1;
    juce::String probeId;
    juce::String category;
    juce::File midiFile;
    int noteCount = 0;
    double windowStartMs = 0.0;
    double windowEndMs = 0.0;
    bool ready = false;
  };

  struct ProbeState
  {
    juce::String probeId;
    int trackNumber = 1;
    juce::String category;
    int64_t timestampMs = 0;
    int midiActivity = 0;
    double noteDensity = 0.0;
    double audioPeak = 0.0;
    ProbeTransportHint transport;
    double lastSeenLocalMs = 0.0;
    ProbeMidiDetails midiDetails;
  };

  struct ToneSpec
  {
    juce::String id;
    juce::String name;
    juce::String source;
    juce::String category; // "Bass", "Lead", "Pad", "Pluck", "FX"
    float oscSine = 0.5f;
    float oscSaw = 0.3f;
    float oscSquare = 0.2f;
    float oscTriangle = 0.1f;
    float oscNoise = 0.0f;
    float oscSub = 0.4f;
    float attack = 0.01f;
    float decay = 0.2f;
    float sustain = 0.6f;
    float release = 0.3f;
    float drive = 0.2f;
    float brightness = 0.5f;
    float stereoWidth = 0.1f;
    float eqLow = 0.5f;
    float eqLowMid = 0.5f;
    float eqHighMid = 0.5f;
    float eqHigh = 0.5f;
  };

  class XiwuSound final : public juce::SynthesiserSound
  {
  public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
  };

  class XiwuVoice final : public juce::SynthesiserVoice
  {
  public:
    XiwuVoice(std::atomic<float>* toneModeParam, std::atomic<float>* masterGainParam);

    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

  private:
    float renderWaveSample(float phase);
    void updateEnvelopeForTone(const ToneSpec& tone);

    std::atomic<float>* toneModeParam = nullptr;
    std::atomic<float>* masterGainParam = nullptr;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;

    float level = 0.0f;
    float angle = 0.0f;
    float angleDelta = 0.0f;
    juce::Random random;
  };

  static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
  static const std::vector<ToneSpec>& getFactoryTones();
  friend class GainPluginAudioProcessorEditor;
  void updateTransportSnapshot();
  juce::String handleOscCommand(const juce::String& address, const juce::String& payloadJson);
  juce::String handlePingRequest() const;
  juce::String handleTransportRequest() const;
  juce::String handleActiveTrackSnapshotRequest() const;
  juce::String handleMidiPlaceRequest(const juce::var& payload);
  juce::String handleMidiCollectFullRequest(const juce::var& payload);    // 全量收集 MIDI
  juce::String handleMidiCollectSelectionRequest(const juce::var& payload); // 选区收集 MIDI
  juce::String handleToneLoadJsonRequest(const juce::var& payload);
  juce::String handleBusySetRequest(const juce::var& payload);
  juce::String handleToolStatusRequest(const juce::var& payload);
  void markExternalGenerationBusy(const juce::String& reason);
  void clearExternalGenerationBusy();
  void ingestProbeStateMessage(const juce::var& payload);
  void pruneExpiredProbes(const double nowMs);
  static juce::String buildJsonReply(const juce::var& value);
  static bool decodeOscAddressAndStringArg(
    const void* data,
    size_t size,
    juce::String& outAddress,
    juce::String& outStringArg);
  static bool readOscPaddedString(
    const uint8_t* data,
    size_t size,
    size_t& offset,
    juce::String& outText);
  int chooseToneIndexForType(const juce::String& toneType) const;
  void applyFloatParamIfPresent(
    const juce::var& source,
    const juce::Identifier& key,
    std::atomic<float>* targetParam);
  void markOscIngress(
    const juce::String& address,
    const juce::String& payloadJson,
    const juce::String& senderIp,
    int senderPort);
  static juce::String buildPayloadPreview(const juce::String& payload, int maxChars);
  void resetMasterEqStates();
  void updateMasterEqCoefficients(double sampleRate);
  void processMasterOutputEq(juce::AudioBuffer<float>& buffer);

  // Probe MIDI export helper
  static juce::File midiTracker_exportMidiFile(
    const juce::File& outputDir,
    const juce::String& trackTag,
    const juce::var& notesJson,
    double windowStartMs,
    double windowEndMs);

  juce::Synthesiser synth;
  juce::AudioProcessorValueTreeState parameters;
  std::atomic<float>* toneModeParam = nullptr;
  std::atomic<float>* masterGainParam = nullptr;
  std::atomic<float>* eqLowParam = nullptr;
  std::atomic<float>* eqLowMidParam = nullptr;
  std::atomic<float>* eqHighMidParam = nullptr;
  std::atomic<float>* eqHighParam = nullptr;
  std::unique_ptr<OscGatewayBridge> oscBridge;
  std::array<float, 2> masterEqLowState{{0.0f, 0.0f}};
  std::array<float, 2> masterEqLowMidState{{0.0f, 0.0f}};
  std::array<float, 2> masterEqHighMidState{{0.0f, 0.0f}};
  float masterEqLowAlpha = 0.985f;
  float masterEqLowMidAlpha = 0.96f;
  float masterEqHighMidAlpha = 0.86f;
  float masterEqCachedSampleRate = 0.0f;

  mutable juce::CriticalSection stateLock;
  TransportSnapshot transportSnapshot;
  juce::File lastPlacedMidiFile;
  int lastPlacedTrack = 1;
  double lastPlacedPpq = 0.0;
  juce::String lastToneType;
  juce::String lastToneSummary;
  juce::String lastOscEndpoint;
  juce::String lastOscPeer;
  juce::String lastGatewayPayloadPreview;
  juce::String pendingClipboardNotice;
  bool hasPendingClipboardNotice = false;

  // Tool operation status from U8 VstDisplayUnit (received via /xiwu/vst/status OSC)
  juce::String toolStatusMessage;
  juce::String toolStatusToolName;
  juce::String toolStatusState;
  juce::String toolStatusHint;
  int toolStatusProgress = 0;
  double toolStatusUpdatedMs = 0.0;
  double lastOscIngressMs = 0.0;
  int64_t oscIngressCount = 0;
  int64_t midiPlacementSeq = 0;
  int64_t midiPlacementConsumedSeq = 0;
  bool externalGenerationBusy = false;
  juce::String externalBusyReason;
  double externalBusyTouchedMs = 0.0;
  int64_t externalBusySeq = 0;

  std::unique_ptr<ProbeUdpBridge> probeBridge;
  int probeUdpPort = 3922;  // UDP port for probe communication
  juce::File probeSessionFile;
  juce::String activeTrackTag;
  std::map<int, ProbeState> probeStateMap;  // key = track_number

  // 播放选区状态
  bool hasSelection = false;
  double selectionStartPpq = 0.0;
  double selectionEndPpq = 0.0;
  double selectionStartMs = 0.0;
  double selectionEndMs = 0.0;

  // Probe MIDI 结果列表（每个 Probe 一个可拖拽块）
  std::vector<ProbeMidiResult> probeMidiResults;
  juce::CriticalSection probeResultsLock;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPluginAudioProcessor)

public:
  // Allow editor access to probe results for rendering
  const std::vector<ProbeMidiResult>& getProbeMidiResults() const { return probeMidiResults; }
};
