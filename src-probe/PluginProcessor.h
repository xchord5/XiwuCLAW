#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
#include <vector>

// Simple debug log function
inline void probeLog(const juce::String& msg)
{
    static juce::File logFile;
    if (!logFile.exists())
    {
        logFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("XiwuCLAW")
                       .getChildFile("probe_debug.log");
    }
    auto now = juce::Time::getCurrentTime();
    logFile.appendText(now.formatted("%H:%M:%S") + " " + msg + "\n");
}

class XiwuProbePluginAudioProcessor final : public juce::AudioProcessor
{
public:
    XiwuProbePluginAudioProcessor();
    ~XiwuProbePluginAudioProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParametersState() { return parameters; }

    // For editor access
    bool isUdpConnected() const { return udpThread && udpThread->isConnected(); }
    int getTrackNumber() const { return trackNumber; }
    juce::String getCategory() const { return category; }

private:
    // ========================================================================
    // MidiActivityTracker - 统计 MIDI 活动
    // ========================================================================
    class MidiActivityTracker
    {
    public:
        MidiActivityTracker() = default;

        void processMidiMessages(const juce::MidiBuffer& midiMessages, double currentTimeMs)
        {
            for (const auto metadata : midiMessages)
            {
                const auto& msg = metadata.getMessage();
                if (msg.isNoteOn())
                {
                    allNotes.push_back({ currentTimeMs, msg.getNoteNumber(), msg.getFloatVelocity(), true });
                }
                else if (msg.isNoteOff())
                {
                    allNotes.push_back({ currentTimeMs, msg.getNoteNumber(), msg.getFloatVelocity(), false });
                }
            }
            pruneOldEvents(currentTimeMs);
        }

        int getMidiActivityCount() const
        {
            return static_cast<int>(events.size());
        }

        double getNoteDensity() const
        {
            if (events.empty())
                return 0.0;
            const auto density = static_cast<double>(events.size()) / (kMidiActivityWindowMs / 100.0);
            return juce::jlimit(0.0, 1.0, density);
        }

        void reset()
        {
            events.clear();
        }

        // 获取指定时间窗口内的 MIDI 详情 JSON
        juce::var getMidiDetails(double windowStartMs, double windowEndMs) const
        {
            auto* arr = new juce::DynamicObject();
            juce::Array<juce::var> notesArray;

            for (const auto& note : allNotes)
            {
                if (note.timeMs < windowStartMs || note.timeMs > windowEndMs)
                    continue;

                auto* noteObj = new juce::DynamicObject();
                noteObj->setProperty("time_ms", note.timeMs - windowStartMs);  // 相对时间
                noteObj->setProperty("note", note.noteNumber);
                noteObj->setProperty("velocity", note.velocity);
                noteObj->setProperty("on", note.isOn);
                notesArray.add(juce::var(noteObj));
            }

            arr->setProperty("window_start_ms", windowStartMs);
            arr->setProperty("window_end_ms", windowEndMs);
            arr->setProperty("notes", juce::var(notesArray));
            arr->setProperty("note_count", notesArray.size());
            return juce::var(arr);
        }

        // 导出指定时间窗口的 MIDI 文件
        juce::File exportMidiFile(const juce::File& outputDir, const juce::String& fileTrackTag,
                                   double windowStartMs, double windowEndMs)
        {
            const juce::String fileName = fileTrackTag + "_" + juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".mid";
            const auto midiFile = outputDir.getChildFile(fileName);

            juce::MidiMessageSequence sequence;
            const auto firstTime = windowStartMs;

            for (const auto& note : allNotes)
            {
                if (note.timeMs < windowStartMs || note.timeMs > windowEndMs)
                    continue;

                const auto deltaMs = note.timeMs - firstTime;
                const auto deltaTicks = static_cast<int>(deltaMs * 0.48); // 120 BPM, 480 ticks/beat
                if (note.isOn)
                {
                    sequence.addEvent(juce::MidiMessage::noteOn(1, note.noteNumber, static_cast<uint8>(note.velocity * 127)), deltaTicks);
                }
                else
                {
                    sequence.addEvent(juce::MidiMessage::noteOff(1, note.noteNumber), deltaTicks);
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

        void clearAllNotes()
        {
            allNotes.clear();
        }

    private:
        struct MidiEvent
        {
            double timeMs;
            int noteNumber;
            float velocity;
        };

        struct NoteEvent
        {
            double timeMs;
            int noteNumber;
            float velocity;
            bool isOn;
        };

        void pruneOldEvents(double currentTimeMs)
        {
            const auto cutoff = currentTimeMs - kMidiActivityWindowMs;
            events.erase(
                std::remove_if(events.begin(), events.end(),
                    [cutoff](const MidiEvent& e) { return e.timeMs < cutoff; }),
                events.end());
        }

        std::vector<MidiEvent> events;
        std::vector<NoteEvent> allNotes;  // 记录所有 MIDI 音符用于导出
        constexpr static double kMidiActivityWindowMs = 500.0;
        constexpr static double kMaxNoteHistoryMs = 60000.0;  // 最多保留 60 秒历史
    };

    // ========================================================================
    // 后台线程处理 UDP 通信 - 命令监听模式
    // ========================================================================
    class UdpThread : private juce::Thread
    {
    public:
        UdpThread()
            : juce::Thread("XiwuProbeUdp")
        {
        }

        ~UdpThread() override
        {
            juce::Thread::stopThread(2000);
        }

        void start(int port, MidiActivityTracker* tracker, XiwuProbePluginAudioProcessor* processor)
        {
            thisPort = port;
            midiTracker = tracker;
            outerProcessor = processor;
            probeLog("UdpThread: Starting on port " + juce::String(port));
            juce::Thread::startThread();
        }

        bool isConnected() const { return socket != nullptr; }
        int getTrackNumber() const { return outerProcessor ? outerProcessor->trackNumber : 1; }

    private:
        void run() override
        {
            probeLog("UdpThread: Listening for commands on port " + juce::String(thisPort));
            socket.reset(new juce::DatagramSocket());
            socket->bindToPort(thisPort);

            uint8_t buffer[2048];
            juce::String sourceIP;
            int sourcePort = 0;

            // 启动时向主插件注册自己
            sendAnnounce();

            // 监听来自主插件的命令
            while (!juce::Thread::threadShouldExit())
            {
                juce::Thread::sleep(50);

                int numBytes = socket->read((void*)buffer, (int)(sizeof(buffer)) - 1, false, sourceIP, sourcePort);
                if (numBytes <= 0)
                    continue;

                buffer[numBytes] = '\0';
                juce::String cmd = juce::String::fromUTF8((const char*)buffer, numBytes).trim();
                probeLog("UdpThread: cmd=" + cmd);

                if (auto parsed = juce::JSON::parse(cmd))
                {
                    if (auto* obj = dynamic_cast<juce::DynamicObject*>(parsed.getDynamicObject()))
                    {
                        if (obj->hasProperty("cmd"))
                        {
                            const auto cmdType = obj->getProperty("cmd").toString();
                            if (cmdType == "get_midi_details")
                            {
                                const double winStart = parseNum(obj, "window_start_ms", 0.0);
                                const double winEnd = parseNum(obj, "window_end_ms", 60000.0);
                                sendMidiDetails(winStart, winEnd);
                            }
                            else if (cmdType == "ping")
                            {
                                sendPong();
                            }
                        }
                    }
                }
            }
        }

        void sendAnnounce()
        {
            if (!socket || !outerProcessor) return;
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            root->setProperty("cmd", juce::var("announce"));
            root->setProperty("category", juce::var(outerProcessor->category));
            root->setProperty("track_number", juce::var(outerProcessor->trackNumber));
            root->setProperty("probe_id", juce::var(outerProcessor->probeId.toString()));
            const auto json = juce::JSON::toString(juce::var(root.get()));
            socket->write("127.0.0.1", mainPluginPort, json.toRawUTF8(), static_cast<int>(json.getNumBytesAsUTF8()));
        }

        void sendMidiDetails(double windowStartMs, double windowEndMs)
        {
            if (!socket || !outerProcessor) return;
            const auto midiDetails = midiTracker->getMidiDetails(windowStartMs, windowEndMs);
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            root->setProperty("category", juce::var(outerProcessor->category));
            root->setProperty("track_number", juce::var(outerProcessor->trackNumber));
            root->setProperty("probe_id", juce::var(outerProcessor->probeId.toString()));
            root->setProperty("timestamp_ms", static_cast<double>(juce::Time::getMillisecondCounterHiRes()));
            root->setProperty("midi_activity", midiTracker->getMidiActivityCount());
            root->setProperty("note_density", midiTracker->getNoteDensity());
            root->setProperty("audio_peak", 0.0);
            root->setProperty("midi_details", midiDetails);
            const auto json = juce::JSON::toString(juce::var(root.get()));
            socket->write("127.0.0.1", mainPluginPort, json.toRawUTF8(), static_cast<int>(json.getNumBytesAsUTF8()));
        }

        void sendPong()
        {
            if (!socket || !outerProcessor) return;
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            root->setProperty("category", juce::var(outerProcessor->category));
            root->setProperty("track_number", juce::var(outerProcessor->trackNumber));
            root->setProperty("probe_id", juce::var(outerProcessor->probeId.toString()));
            root->setProperty("pong", juce::var(true));
            const auto json = juce::JSON::toString(juce::var(root.get()));
            socket->write("127.0.0.1", mainPluginPort, json.toRawUTF8(), static_cast<int>(json.getNumBytesAsUTF8()));
        }

        static double parseNum(const juce::DynamicObject* obj, const juce::String& key, double fallback)
        {
            return obj->hasProperty(key) ? static_cast<double>(obj->getProperty(key)) : fallback;
        }

        int thisPort = 3922;
        int mainPluginPort = 3922;
        std::unique_ptr<juce::DatagramSocket> socket;
        MidiActivityTracker* midiTracker = nullptr;
        XiwuProbePluginAudioProcessor* outerProcessor = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UdpThread)
    };

    // ========================================================================
    // Member variables
    // ========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    int readPortFromSessionFile();

    juce::AudioProcessorValueTreeState parameters;

    std::unique_ptr<UdpThread> udpThread;
    std::unique_ptr<MidiActivityTracker> midiTracker;

    juce::String category;       // "Drums", "Rhythm", "Melody"
    int trackNumber = 1;         // 轨道号 (1-64)
    juce::Uuid probeId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XiwuProbePluginAudioProcessor)
};
