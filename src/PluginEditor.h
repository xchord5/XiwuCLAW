#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>

#include "PluginProcessor.h"

// 科技感配色方案 - 赛博朋克风格
namespace TechColours
{
inline juce::Colour bgTop()      { return juce::Colour(0xff0a0e1a); }  // 深空黑蓝
inline juce::Colour bgMid()      { return juce::Colour(0xff141b2e); }  // 午夜蓝
inline juce::Colour bgBottom()   { return juce::Colour(0xff0f1524); }  // 深渊黑
inline juce::Colour panel()      { return juce::Colour(0xff1a2235); }  // 面板底色
inline juce::Colour panelDark()  { return juce::Colour(0xff0f1420); }  // 深色面板
inline juce::Colour panelBorder(){ return juce::Colour(0xff2d3850); }  // 面板边框

// 霓虹强调色
inline juce::Colour accentCyan()   { return juce::Colour(0xff00f0ff); }  // 赛博青
inline juce::Colour accentPink()   { return juce::Colour(0xffff00aa); }  // 霓虹粉
inline juce::Colour accentPurple() { return juce::Colour(0xff9d00ff); }  // 电紫
inline juce::Colour accentGreen()  { return juce::Colour(0xff00ff88); }  // 矩阵绿
inline juce::Colour accentOrange() { return juce::Colour(0xffff8800); }  // 琥珀橙

// 发光效果色
inline juce::Colour glowCyan()   { return juce::Colour(0x4000f0ff); }
inline juce::Colour glowPink()   { return juce::Colour(0x40ff00aa); }
inline juce::Colour glowPurple() { return juce::Colour(0x409d00ff); }
}

// 粒子效果结构
struct Particle {
    juce::Point<float> position;
    juce::Point<float> velocity;
    float size;
    float alpha;
    juce::Colour colour;

    void update(float dt, int width, int height) {
        position += velocity * dt;
        // 边界反弹
        if (position.x < 0 || position.x > width) velocity.x *= -1;
        if (position.y < 0 || position.y > height) velocity.y *= -1;
        // 限制在边界内
        position.x = juce::jlimit(0.0f, (float)width, position.x);
        position.y = juce::jlimit(0.0f, (float)height, position.y);
    }
};

// Probe MIDI 结果列表模型
class ProbeResultsListModel : public juce::ListBoxModel
{
public:
    explicit ProbeResultsListModel(GainPluginAudioProcessor* proc) : processor(proc) {}

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent& event) override;

    // 手动拖拽支持 - 在 listBoxItemClicked 中检测鼠标拖动
    void startDragForRow(int rowNumber, const juce::MouseEvent& event);

private:
    GainPluginAudioProcessor* processor;
    int lastDragRow = -1;  // 记录拖拽开始的行
};

class GainPluginAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer,
                                             public juce::DragAndDropContainer
{
public:
    explicit GainPluginAudioProcessorEditor(GainPluginAudioProcessor&);
    ~GainPluginAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class Page
    {
        Stage,
        Eq
    };

    static constexpr int kGridCols = 120;
    static constexpr int kGridRows = 80;

    void timerCallback() override;
    void switchPage(Page page);
    void updatePageVisibility();
    void styleKnob(juce::Slider& slider, const juce::Colour& accent);
    void refreshStatusText();
    void refreshToolStatus();  // Update tool operation status display
    void startClipboardToast(const juce::String& text);
    void paintClipboardToast(juce::Graphics& g) const;
    void paintStageMatrixArea(juce::Graphics& g);
    bool pointToGridCell(juce::Point<int> pos, int& outCol, int& outRow) const;
    void paintGridCell(int col, int row, bool erase);
    bool isGridCellPainted(int col, int row) const;
    uint8_t getGridCellLevel(int col, int row) const;
    void clearGridCanvas();
    void cacheGridAndShowBusyMarker(const juce::String& reason);
    void restoreGridFromBusyMarker();
    void drawBusyMarkerOnGrid(const juce::String& reason);
    void sendGridToGatewayAsync(
        int activeCells,
        std::vector<uint8_t> levelsSnapshot,
        std::vector<uint8_t> paintedSnapshot);
    void startExternalMidiDrag();

    // === 科技感 UI 特效方法 ===
    void drawNeonGlow(juce::Graphics& g, juce::Rectangle<int> bounds,
                      juce::Colour colour, float intensity = 1.0f);
    void paintGridBackground(juce::Graphics& g, const juce::Rectangle<int>& area);
    void paintTechKnob(juce::Graphics& g, juce::Slider& slider,
                       const juce::Rectangle<int>& bounds, juce::Colour accent);
    void paintStatusIndicator(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                              bool active, juce::Colour colour);
    void initParticles();
    void updateAndDrawParticles(juce::Graphics& g);
    void drawAnimatedTab(juce::Graphics& g, juce::TextButton& button,
                         bool isActive, juce::Colour colour);
    void refreshProbeInfo();    // 更新 Probe MIDI 详情显示
    void refreshProbeResultsList();  // 刷新可拖拽 MIDI 块列表
    void updateSelectionStatus();    // 更新选区状态显示

    GainPluginAudioProcessor& processor;

    juce::Image bannerImage;

    juce::TextButton stagePageButton;
    juce::TextButton eqPageButton;

    juce::Label toneLabel;
    juce::ComboBox toneSelector;
    juce::Label gainLabel;
    juce::Slider masterGainSlider;

    juce::Label eqLowLabel;
    juce::Slider eqLowSlider;
    juce::Label eqLowMidLabel;
    juce::Slider eqLowMidSlider;
    juce::Label eqHighMidLabel;
    juce::Slider eqHighMidSlider;
    juce::Label eqHighLabel;
    juce::Slider eqHighSlider;

    juce::TextButton generateGridButton;
    juce::Label gridMetaLabel;
    juce::Label gatewayLabel;

    juce::TextEditor statusView;
    juce::Label toolStatusLabel;  // Tool operation status display (from U8 VstDisplayUnit)
    juce::Label probeInfoLabel;      // 显示 Probe MIDI 详情
    juce::ListBox probeResultsList;  // 蓝色可拖拽 MIDI 块列表
    juce::TextButton getSelectionButton;  // 获取选区 MIDI 按钮
    std::unique_ptr<ProbeResultsListModel> probeResultsModel;  // Probe 结果列表模型

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> toneAttachment;
    std::unique_ptr<SliderAttachment> masterGainAttachment;
    std::unique_ptr<SliderAttachment> eqLowAttachment;
    std::unique_ptr<SliderAttachment> eqLowMidAttachment;
    std::unique_ptr<SliderAttachment> eqHighMidAttachment;
    std::unique_ptr<SliderAttachment> eqHighAttachment;

    Page currentPage = Page::Stage;
    float glowPhase = 0.0f;
    bool clipboardToastVisible = false;
    juce::String clipboardToastText;
    double clipboardToastStartMs = 0.0;

    std::vector<uint8_t> gridLevels;
    std::vector<uint8_t> gridPainted;
    bool isGridPainting = false;
    bool gridEraseMode = false;
    int lastGridPaintCol = -1;
    int lastGridPaintRow = -1;

    juce::Rectangle<int> stageGridArea;
    juce::Rectangle<int> midiTileArea;
    juce::Point<int> midiDragStart;
    bool midiTilePressed = false;
    bool midiExternalDragStarted = false;

    juce::File latestGeneratedMidi;
    int latestGeneratedNoteCount = 0;
    int latestGeneratedActiveCells = 0;
    juce::String latestGatewayReply;
    bool gridSending = false;
    bool externalBusy = false;
    bool externalBusyOverlayActive = false;
    juce::String externalBusyReason;
    std::vector<uint8_t> cachedGridLevels;
    std::vector<uint8_t> cachedGridPainted;
    juce::String cachedGridMetaLabelText;
    bool cachedGridMetaLabelValid = false;
    int statusRefreshCounter = 0;
    int sendingDots = 0;
    juce::Rectangle<int> animatedDirtyArea;

    // 粒子系统
    std::array<Particle, 30> particles;
    double lastParticleUpdate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainPluginAudioProcessorEditor)
};
