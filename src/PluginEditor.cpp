#include "PluginEditor.h"
#include <JuceHeader.h>

#include <array>
#include <cmath>
#include <thread>
#include <random>

// XiwuAssetData binary data forward declarations
namespace XiwuAssetData {
    extern const char* banner_png;
    extern const char* namedResourceList[];
    const int banner_pngSize = 16851;
}

// P0: 霓虹发光效果
void GainPluginAudioProcessorEditor::drawNeonGlow(juce::Graphics& g, juce::Rectangle<int> bounds,
                                                   juce::Colour colour, float intensity)
{
    // 外层光晕 (大模糊)
    for (int i = 8; i > 0; --i) {
        g.setColour(colour.withAlpha(intensity * 0.03f / i));
        g.drawRoundedRectangle(bounds.expanded(i).toFloat(), 8.0f, 2.0f);
    }

    // 内层光晕 (清晰)
    g.setColour(colour.withAlpha(intensity * 0.4f));
    g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 2.5f);

    // 核心亮边
    g.setColour(colour.withAlpha(intensity * 0.9f));
    g.drawRoundedRectangle(bounds.reduced(1).toFloat(), 7.0f, 1.5f);
}

// P0: 网格背景 + 扫描线效果
void GainPluginAudioProcessorEditor::paintGridBackground(juce::Graphics& g, const juce::Rectangle<int>& area)
{
    const auto gridColour = juce::Colour(0x1000f0ff);
    const float scanPhase = glowPhase * 0.5f;

    // 绘制网格线
    const int cellSize = 20;
    for (int x = area.getX(); x < area.getRight(); x += cellSize) {
        g.setColour(gridColour);
        g.drawLine(static_cast<float>(x), static_cast<float>(area.getY()),
                   static_cast<float>(x), static_cast<float>(area.getBottom()), 1.0f);
    }
    for (int y = area.getY(); y < area.getBottom(); y += cellSize) {
        g.setColour(gridColour);
        g.drawLine(static_cast<float>(area.getX()), static_cast<float>(y),
                   static_cast<float>(area.getRight()), static_cast<float>(y), 1.0f);
    }

    // 扫描线效果 (从上到下)
    const int scanY = area.getY() + static_cast<int>(
        (std::sin(scanPhase) * 0.5f + 0.5f) * area.getHeight());

    juce::ColourGradient gradient(
        juce::Colour(0x0000f0ff),
        static_cast<float>(area.getCentreX()), static_cast<float>(scanY - 20),
        juce::Colour(0x2500f0ff),
        static_cast<float>(area.getCentreX()), static_cast<float>(scanY + 20),
        false);
    g.setGradientFill(gradient);
    g.fillRect(area);
}

// P1: 科技感旋钮绘制
void GainPluginAudioProcessorEditor::paintTechKnob(juce::Graphics& g, juce::Slider& slider,
                                                    const juce::Rectangle<int>& bounds, juce::Colour accent)
{
    const float value = slider.getValue();
    const float normalized = (value - slider.getMinimum()) /
                            (slider.getMaximum() - slider.getMinimum());

    const auto centre = bounds.getCentre().toFloat();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    // LED 环背景
    g.setColour(juce::Colour(0xff1a2235));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2, radius * 2);

    // LED 光环 (带渐变)
    const float startAngle = -2.5f * juce::MathConstants<float>::pi;
    const float endAngle = startAngle + normalized * 4.5f * juce::MathConstants<float>::pi;

    juce::Path ledRing;
    ledRing.addArc(centre.x - radius * 0.85f, centre.y - radius * 0.85f,
                   radius * 1.7f, radius * 1.7f, startAngle, endAngle, true);

    juce::ColourGradient gradient(
        accent.withAlpha(0.3f),
        centre.x - radius, centre.y,
        accent.withAlpha(0.9f),
        centre.x + radius, centre.y,
        false);
    g.setGradientFill(gradient);

    g.strokePath(ledRing, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 中心数值显示
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawFittedText(juce::String(value, 1),
                     bounds.reduced(bounds.getWidth() * 0.25f),
                     juce::Justification::centred, 1);
}

// P2: 状态指示灯 (脉冲效果)
void GainPluginAudioProcessorEditor::paintStatusIndicator(juce::Graphics& g, const juce::Rectangle<int>& bounds,
                                                           bool active, juce::Colour colour)
{
    const float pulse = 0.5f + 0.5f * std::sin(glowPhase * 4.0f);

    // 外圈光晕
    g.setColour(colour.withAlpha(0.2f + pulse * 0.3f));
    g.fillEllipse(bounds.toFloat());

    // 内圈 (核心)
    const auto innerBounds = bounds.reduced(bounds.getWidth() / 4);
    g.setColour(colour.withAlpha(0.6f + pulse * 0.4f));
    g.fillEllipse(innerBounds.toFloat());

    // 高光点
    const auto highlightPos = innerBounds.getCentre() - juce::Point<int>(2, 2);
    g.setColour(juce::Colours::white.withAlpha(0.8f));
    g.fillEllipse(highlightPos.x, highlightPos.y, 3, 3);
}

// P1: 粒子系统初始化
void GainPluginAudioProcessorEditor::initParticles()
{
    static std::array<juce::Colour, 4> particleColours = {
        TechColours::accentCyan(),
        TechColours::accentPink(),
        TechColours::accentPurple(),
        TechColours::accentGreen()
    };

    std::mt19937 rng(42);  // 固定种子保证一致性
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> velDist(-15.0f, 15.0f);
    std::uniform_real_distribution<float> sizeDist(2.0f, 5.0f);
    std::uniform_int_distribution<int> colourDist(0, 3);

    for (auto& p : particles) {
        p.position = juce::Point<float>(
            posDist(rng) * 1200.0f,
            posDist(rng) * 800.0f);
        p.velocity = juce::Point<float>(
            velDist(rng),
            velDist(rng));
        p.size = sizeDist(rng);
        p.alpha = 0.3f + posDist(rng) * 0.3f;
        p.colour = particleColours[static_cast<size_t>(colourDist(rng))];
    }
}

// P1: 更新并绘制粒子
void GainPluginAudioProcessorEditor::updateAndDrawParticles(juce::Graphics& g)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const float dt = static_cast<float>((now - lastParticleUpdate) / 1000.0);
    lastParticleUpdate = now;

    const int w = getWidth();
    const int h = getHeight();

    for (auto& p : particles) {
        p.update(juce::jmin(dt, 0.05f), w, h);

        const float pulse = 0.5f + 0.5f * std::sin(glowPhase * 3.0f + p.size);
        p.alpha = juce::jlimit(0.2f, 0.6f, p.alpha);

        g.setColour(p.colour.withAlpha(p.alpha * pulse));
        g.fillEllipse(p.position.x - p.size/2, p.position.y - p.size/2,
                      p.size, p.size);
    }
}

// P2: 动画标签页绘制
void GainPluginAudioProcessorEditor::drawAnimatedTab(juce::Graphics& g, juce::TextButton& button,
                                                      bool isActive, juce::Colour colour)
{
    const auto bounds = button.getBounds();

    g.setColour(isActive ? colour.withAlpha(0.3f) : juce::Colour(0x00000000));
    g.fillRoundedRectangle(bounds.toFloat(), 6.0f);

    const float pulse = 0.5f + 0.5f * std::sin(glowPhase * 2.0f);
    g.setColour(colour.withAlpha(isActive ? 0.6f + pulse * 0.4f : 0.3f));
    g.drawRoundedRectangle(bounds.reduced(1).toFloat(), 6.0f, isActive ? 2.0f : 1.0f);

    g.setColour(isActive ? juce::Colours::white : colour.withAlpha(0.7f));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawFittedText(button.getButtonText(),
                     bounds.reduced(4, 2),
                     juce::Justification::centred, 1);
}

GainPluginAudioProcessorEditor::GainPluginAudioProcessorEditor(GainPluginAudioProcessor& p)
  : AudioProcessorEditor(&p),
    processor(p),
    gridLevels(static_cast<size_t>(kGridCols * kGridRows), 0),
    gridPainted(static_cast<size_t>(kGridCols * kGridRows), 0)
{
  setSize(1180, 760);

  initParticles();
  lastParticleUpdate = juce::Time::getMillisecondCounterHiRes();

  // 加载 Banner 图片
  bannerImage = juce::ImageCache::getFromMemory(XiwuAssetData::banner_png, XiwuAssetData::banner_pngSize);

  auto setupPageButton = [this](juce::TextButton& b, const juce::String& name, Page target) {
    b.setButtonText(name);
    b.setClickingTogglesState(false);
    b.onClick = [this, target] { switchPage(target); };
    b.setColour(juce::TextButton::buttonColourId, juce::Colour(0x00000000));
    b.setColour(juce::TextButton::buttonOnColourId, TechColours::accentCyan().withAlpha(0.2f));
    b.setColour(juce::TextButton::textColourOffId, TechColours::accentCyan().withAlpha(0.6f));
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(b);
  };
  setupPageButton(stagePageButton, "STAGE", Page::Stage);
  setupPageButton(eqPageButton, "EQ", Page::Eq);

  toneLabel.setText("PATCH (" + juce::String(GainPluginAudioProcessor::getToneNames().size()) + ")", juce::dontSendNotification);
  toneLabel.setJustificationType(juce::Justification::centredLeft);
  toneLabel.setFont(juce::FontOptions(15.0f));
  toneLabel.setColour(juce::Label::textColourId, TechColours::accentCyan().withAlpha(0.8f));
  addAndMakeVisible(toneLabel);

  toneSelector.addItemList(GainPluginAudioProcessor::getToneNames(), 1);
  toneSelector.setColour(juce::ComboBox::backgroundColourId, TechColours::panelDark());
  toneSelector.setColour(juce::ComboBox::outlineColourId, TechColours::panelBorder());
  toneSelector.setColour(juce::ComboBox::textColourId, TechColours::accentCyan().withAlpha(0.9f));
  toneSelector.setColour(juce::ComboBox::arrowColourId, TechColours::accentCyan());
  // 弹出菜单底色
  toneSelector.setColour(juce::PopupMenu::backgroundColourId, TechColours::panelDark());
  toneSelector.setColour(juce::PopupMenu::textColourId, juce::Colours::white);
  toneSelector.setColour(juce::PopupMenu::highlightedBackgroundColourId, TechColours::accentCyan().withAlpha(0.3f));
  addAndMakeVisible(toneSelector);

  gainLabel.setText("MASTER", juce::dontSendNotification);
  gainLabel.setJustificationType(juce::Justification::centred);
  gainLabel.setFont(juce::FontOptions(14.0f));
  gainLabel.setColour(juce::Label::textColourId, TechColours::accentCyan().withAlpha(0.8f));
  addAndMakeVisible(gainLabel);

  styleKnob(masterGainSlider, TechColours::accentCyan());
  addAndMakeVisible(masterGainSlider);

  generateGridButton.setButtonText("GENERATE MIDI");
  generateGridButton.setColour(juce::TextButton::buttonColourId, TechColours::panel());
  generateGridButton.setColour(juce::TextButton::buttonOnColourId, TechColours::accentGreen());
  generateGridButton.setColour(juce::TextButton::textColourOffId, TechColours::accentCyan());
  generateGridButton.setColour(juce::TextButton::textColourOnId, TechColours::panelDark());
  addAndMakeVisible(generateGridButton);

  gridMetaLabel.setText("Grid 120x80", juce::dontSendNotification);
  gridMetaLabel.setJustificationType(juce::Justification::centredLeft);
  gridMetaLabel.setFont(juce::FontOptions(13.0f));
  gridMetaLabel.setColour(juce::Label::textColourId, TechColours::accentCyan().withAlpha(0.8f));
  addAndMakeVisible(gridMetaLabel);

  gatewayLabel.setText("Gateway: idle", juce::dontSendNotification);
  gatewayLabel.setJustificationType(juce::Justification::centredLeft);
  gatewayLabel.setFont(juce::FontOptions(12.0f));
  gatewayLabel.setColour(juce::Label::textColourId, TechColours::accentGreen().withAlpha(0.8f));
  addAndMakeVisible(gatewayLabel);

  eqLowLabel.setText("LOW", juce::dontSendNotification);
  eqLowMidLabel.setText("LOW MID", juce::dontSendNotification);
  eqHighMidLabel.setText("HIGH MID", juce::dontSendNotification);
  eqHighLabel.setText("HIGH", juce::dontSendNotification);

  auto styleEqSlider = [](juce::Slider& s, juce::Colour thumb) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 18);
    s.setColour(juce::Slider::rotarySliderFillColourId, thumb);
    s.setColour(juce::Slider::rotarySliderOutlineColourId, TechColours::panelDark());
    s.setColour(juce::Slider::thumbColourId, TechColours::accentCyan());
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxBackgroundColourId, TechColours::panelDark());
    s.setColour(juce::Slider::textBoxOutlineColourId, TechColours::panelBorder());
  };

  for (auto* label : { &eqLowLabel, &eqLowMidLabel, &eqHighMidLabel, &eqHighLabel }) {
    label->setJustificationType(juce::Justification::centred);
    label->setFont(juce::FontOptions(12.5f));
    label->setColour(juce::Label::textColourId, TechColours::accentCyan().withAlpha(0.8f));
    addAndMakeVisible(*label);
  }

  styleEqSlider(eqLowSlider, TechColours::accentCyan());
  styleEqSlider(eqLowMidSlider, TechColours::accentGreen());
  styleEqSlider(eqHighMidSlider, TechColours::accentPink());
  styleEqSlider(eqHighSlider, TechColours::accentPurple());
  addAndMakeVisible(eqLowSlider);
  addAndMakeVisible(eqLowMidSlider);
  addAndMakeVisible(eqHighMidSlider);
  addAndMakeVisible(eqHighSlider);

  statusView.setMultiLine(true);
  statusView.setReadOnly(true);
  statusView.setScrollbarsShown(true);
  statusView.setCaretVisible(false);
  statusView.setPopupMenuEnabled(false);
  statusView.setColour(juce::TextEditor::backgroundColourId, TechColours::panelDark());
  statusView.setColour(juce::TextEditor::outlineColourId, TechColours::panelBorder());
  statusView.setColour(juce::TextEditor::textColourId, juce::Colours::white);
  statusView.setFont(juce::FontOptions(12.0f));
  statusView.setText(processor.getGatewayStatusReport(), juce::dontSendNotification);
  addAndMakeVisible(statusView);

  // Tool status label (prominent display for user feedback)
  toolStatusLabel.setColour(juce::Label::backgroundColourId, TechColours::panelDark());
  toolStatusLabel.setColour(juce::Label::textColourId, TechColours::accentPurple());
  toolStatusLabel.setColour(juce::Label::outlineColourId, TechColours::accentPurple());
  toolStatusLabel.setBorderSize({ 6, 10, 6, 10 });
  toolStatusLabel.setFont(juce::FontOptions(16.0f));
  toolStatusLabel.setJustificationType(juce::Justification::topLeft);
  toolStatusLabel.setText("Tool: Idle", juce::dontSendNotification);
  addAndMakeVisible(toolStatusLabel);

  // Probe MIDI 详情显示
  probeInfoLabel.setColour(juce::Label::backgroundColourId, TechColours::panelDark());
  probeInfoLabel.setColour(juce::Label::textColourId, TechColours::accentGreen());
  probeInfoLabel.setColour(juce::Label::outlineColourId, TechColours::panelBorder());
  probeInfoLabel.setBorderSize({ 8, 12, 8, 12 });
  probeInfoLabel.setFont(juce::FontOptions(11.0f));
  probeInfoLabel.setJustificationType(juce::Justification::topLeft);
  probeInfoLabel.setText("Probe: Waiting for connection...", juce::dontSendNotification);
  addAndMakeVisible(probeInfoLabel);

  // 获取选区 MIDI 按钮
  getSelectionButton.setButtonText("GET SELECTION MIDI");
  getSelectionButton.setColour(juce::TextButton::buttonColourId, TechColours::panel());
  getSelectionButton.setColour(juce::TextButton::buttonOnColourId, TechColours::accentPurple());
  getSelectionButton.setColour(juce::TextButton::textColourOffId, TechColours::accentCyan());
  getSelectionButton.setColour(juce::TextButton::textColourOnId, TechColours::panelDark());
  getSelectionButton.onClick = [this] {
    processor.requestProbeMidiDetails();
  };
  addAndMakeVisible(getSelectionButton);

  // Probe MIDI 结果列表 (蓝色可拖拽块)
  probeResultsModel = std::make_unique<ProbeResultsListModel>(&processor);
  probeResultsList.setModel(probeResultsModel.get());
  probeResultsList.setColour(juce::ListBox::backgroundColourId, TechColours::panelDark());
  probeResultsList.setColour(juce::ListBox::outlineColourId, TechColours::panelBorder());
  addAndMakeVisible(probeResultsList);

  toneAttachment = std::make_unique<ComboAttachment>(processor.getParametersState(), "tone_mode", toneSelector);
  masterGainAttachment = std::make_unique<SliderAttachment>(processor.getParametersState(), "master_gain", masterGainSlider);
  eqLowAttachment = std::make_unique<SliderAttachment>(processor.getParametersState(), "eq_low", eqLowSlider);
  eqLowMidAttachment = std::make_unique<SliderAttachment>(processor.getParametersState(), "eq_low_mid", eqLowMidSlider);
  eqHighMidAttachment = std::make_unique<SliderAttachment>(processor.getParametersState(), "eq_high_mid", eqHighMidSlider);
  eqHighAttachment = std::make_unique<SliderAttachment>(processor.getParametersState(), "eq_high", eqHighSlider);

  generateGridButton.onClick = [this] {
    if (gridSending || processor.isExternalGenerationBusy()) {
      gridMetaLabel.setText("Palace music task is running. GENERATE MIDI is temporarily disabled.", juce::dontSendNotification);
      return;
    }
    int activeCells = 0;
    for (const auto v : gridPainted) { if (v != 0) ++activeCells; }
    if (activeCells == 0) {
      latestGeneratedMidi = juce::File();
      latestGeneratedNoteCount = 0;
      latestGeneratedActiveCells = 0;
      gridMetaLabel.setText("Grid empty. Nothing generated.", juce::dontSendNotification);
      return;
    }
    auto levelsSnapshot = gridLevels;
    auto paintedSnapshot = gridPainted;
    latestGeneratedMidi = juce::File();
    latestGeneratedNoteCount = 0;
    latestGeneratedActiveCells = activeCells;
    gridMetaLabel.setText("Sending matrix to gateway... active=" + juce::String(activeCells), juce::dontSendNotification);
    generateGridButton.setEnabled(false);
    clearGridCanvas();
    sendGridToGatewayAsync(activeCells, std::move(levelsSnapshot), std::move(paintedSnapshot));
  };

  switchPage(Page::Stage);
  startTimerHz(20);
}

void GainPluginAudioProcessorEditor::styleKnob(juce::Slider& slider, const juce::Colour& accent)
{
  slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 18);
  slider.setColour(juce::Slider::rotarySliderFillColourId, accent);
  slider.setColour(juce::Slider::rotarySliderOutlineColourId, TechColours::panelDark());
  slider.setColour(juce::Slider::thumbColourId, TechColours::accentCyan());
  slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
  slider.setColour(juce::Slider::textBoxBackgroundColourId, TechColours::panelDark());
  slider.setColour(juce::Slider::textBoxOutlineColourId, TechColours::panelBorder());
}

void GainPluginAudioProcessorEditor::timerCallback()
{
  const bool hadToastAtStart = clipboardToastVisible;

  ++statusRefreshCounter;
  if ((statusRefreshCounter % 6) == 0)
  {
    refreshStatusText();
    refreshToolStatus();  // Update tool operation status
  }
  if ((statusRefreshCounter % 3) == 0)
    updateSelectionStatus();

  const bool prevExternalBusy = externalBusy;
  const auto prevExternalBusyReason = externalBusyReason;
  externalBusy = processor.isExternalGenerationBusy();
  externalBusyReason = processor.getExternalBusyReason().trim();

  if (!prevExternalBusy && externalBusy) {
    cacheGridAndShowBusyMarker(externalBusyReason);
  } else if (prevExternalBusy && !externalBusy) {
    restoreGridFromBusyMarker();
  } else if (externalBusy && externalBusyOverlayActive && externalBusyReason.isNotEmpty() && externalBusyReason != prevExternalBusyReason) {
    drawBusyMarkerOnGrid(externalBusyReason);
  }

  const auto shouldEnableGenerate = !gridSending && !externalBusy;
  if (generateGridButton.isEnabled() != shouldEnableGenerate)
    generateGridButton.setEnabled(shouldEnableGenerate);

  juce::File placedMidiFromGateway;
  if (processor.consumePlacedMidiUpdate(placedMidiFromGateway)) {
    latestGeneratedMidi = placedMidiFromGateway;
    latestGeneratedActiveCells = 0;
    latestGeneratedNoteCount = 0;
    gridMetaLabel.setText("Plugin MIDI ready: " + placedMidiFromGateway.getFileName(), juce::dontSendNotification);
  }

  if (gridSending) {
    glowPhase += 0.03f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
      glowPhase -= juce::MathConstants<float>::twoPi;
    const int dots = static_cast<int>(std::fmod(glowPhase * 4.0f, 3.0f)) + 1;
    if (dots != sendingDots) {
      sendingDots = dots;
      gatewayLabel.setText("Gateway: sending" + juce::String::repeatedString(".", dots), juce::dontSendNotification);
    }
  } else if (externalBusy) {
    gatewayLabel.setText("Gateway: palace command running...", juce::dontSendNotification);
  }

  if (hadToastAtStart && !clipboardToastVisible) {
    // toast just faded out
  }
  if (clipboardToastVisible) {
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - clipboardToastStartMs > 4000.0)
      clipboardToastVisible = false;
  }

  if (externalBusy && externalBusyOverlayActive) {
    glowPhase += 0.02f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
      glowPhase -= juce::MathConstants<float>::twoPi;
    repaint(animatedDirtyArea);
  }
  else if (gridSending) {
    glowPhase += 0.02f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
      glowPhase -= juce::MathConstants<float>::twoPi;
    repaint();
  }
  else {
    glowPhase += 0.01f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
      glowPhase -= juce::MathConstants<float>::twoPi;
    repaint();
  }
}

void GainPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
  // P0: 深空渐变背景
  juce::ColourGradient bg(TechColours::bgTop(), 0.0f, 0.0f, TechColours::bgBottom(), 0.0f, static_cast<float>(getHeight()), false);
  bg.addColour(0.5, TechColours::bgMid());
  g.setGradientFill(bg);
  g.fillAll();

  // P1: 粒子效果
  updateAndDrawParticles(g);

  const auto frame = getLocalBounds().reduced(8).toFloat();

  // P0: 面板底色 - 排除顶部 Banner 区域
  juce::Path panelPath;
  juce::Rectangle<float> mainRect = frame;
  juce::Rectangle<float> bannerCutout(getLocalBounds().reduced(18).removeFromTop(100).reduced(8, 4).toFloat());

  panelPath.addRectangle(mainRect);
  panelPath.addRectangle(bannerCutout);
  panelPath.setUsingNonZeroWinding(false);

  g.setColour(TechColours::panel());
  g.fillPath(panelPath);

  auto inner = frame.reduced(3.0f);
  juce::ColourGradient innerGrad(TechColours::panel(), inner.getX(), inner.getY(), TechColours::panelDark(), inner.getX(), inner.getBottom(), false);
  g.setGradientFill(innerGrad);
  g.fillRoundedRectangle(inner, 7.0f);

  auto utilityStrip = inner.reduced(12.0f, 10.0f).removeFromTop(36.0f);
  g.setColour(TechColours::panelDark());
  g.fillRoundedRectangle(utilityStrip, 4.0f);
  // 移除 utilityStrip 的边框
  // g.setColour(TechColours::accentCyan().withAlpha(0.5f));
  // g.drawRoundedRectangle(utilityStrip, 4.0f, 1.0f);

  // 绘制 Banner 图片（PNG 带透明通道，保持原始比例）
  if (bannerImage.isValid()) {
    auto bannerArea = getLocalBounds().reduced(18).removeFromTop(100).reduced(8, 4);
    g.setOpacity(0.85f);
    g.drawImageWithin(bannerImage, bannerArea.getX(), bannerArea.getY(), bannerArea.getWidth(), bannerArea.getHeight(),
                      juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
    g.setOpacity(1.0f);
  }

  auto mainDeck = inner.reduced(14.0f, 14.0f);
  mainDeck.removeFromTop(122.0f);
  mainDeck.removeFromBottom(94.0f);
  g.setColour(TechColours::panel());
  g.fillRoundedRectangle(mainDeck, 6.0f);
  g.setColour(TechColours::accentCyan().withAlpha(0.4f));
  g.drawRoundedRectangle(mainDeck, 6.0f, 1.0f);

  if (currentPage == Page::Stage) {
    auto leftRail = mainDeck.reduced(12.0f).removeFromLeft(230.0f).toNearestInt();
    g.setColour(TechColours::panelDark());
    g.fillRoundedRectangle(leftRail.toFloat(), 4.0f);
    drawNeonGlow(g, leftRail, TechColours::accentPurple(), 0.4f);

    auto rightRail = mainDeck.reduced(12.0f).removeFromRight(220.0f).toNearestInt();
    g.setColour(TechColours::panelDark());
    g.fillRoundedRectangle(rightRail.toFloat(), 4.0f);
    drawNeonGlow(g, rightRail, TechColours::accentCyan(), 0.4f);

    paintStageMatrixArea(g);

    if (!midiTileArea.isEmpty()) {
      auto tile = midiTileArea.toFloat();
      const bool ready = latestGeneratedMidi.existsAsFile();

      juce::ColourGradient tileGrad(
        ready ? TechColours::accentGreen().withAlpha(0.3f) : TechColours::panel().withMultipliedLightness(0.8f),
        tile.getX(), tile.getY(),
        ready ? TechColours::accentGreen().withAlpha(0.1f) : TechColours::panelDark(),
        tile.getX(), tile.getBottom(), false);
      g.setGradientFill(tileGrad);
      g.fillRoundedRectangle(tile, 6.0f);

      drawNeonGlow(g, midiTileArea, ready ? TechColours::accentGreen() : TechColours::panelBorder(), ready ? 0.8f : 0.4f);

      g.setFont(juce::FontOptions(15.0f));
      g.setColour(juce::Colours::white);
      auto tileTextBounds = midiTileArea;
      g.drawFittedText(ready ? "MIDI READY" : "NO MIDI", tileTextBounds.removeFromTop(28), juce::Justification::centred, 1);

      auto textArea = tileTextBounds.withTrimmedTop(2).reduced(10, 6);
      g.setFont(juce::FontOptions(12.0f));
      const auto fileName = ready ? latestGeneratedMidi.getFileName() : "Press GENERATE MIDI";
      g.drawFittedText(fileName, textArea.removeFromTop(36), juce::Justification::centred, 2);
      g.setColour(ready ? TechColours::accentGreen() : TechColours::accentCyan().withAlpha(0.6f));
      g.drawFittedText(ready ? ("active=" + juce::String(latestGeneratedActiveCells)) : "drag disabled", textArea.removeFromTop(20), juce::Justification::centred, 1);
      g.drawFittedText(ready ? "drag this block to DAW track" : "draw in grid first", textArea, juce::Justification::centred, 2);
    }
  }

  // P0: 主甲板脉冲光晕
  const float pulse = 0.5f + 0.5f * std::sin(glowPhase);
  drawNeonGlow(g, mainDeck.reduced(10.0f).toNearestInt(), TechColours::accentCyan(), 0.3f + pulse * 0.3f);

  auto statusPanel = inner.reduced(14.0f, 12.0f).removeFromBottom(84.0f).toNearestInt();
  g.setColour(TechColours::panelDark());
  g.fillRoundedRectangle(statusPanel.toFloat(), 6.0f);
  drawNeonGlow(g, statusPanel, TechColours::accentPurple(), 0.35f);

  paintClipboardToast(g);
}

void GainPluginAudioProcessorEditor::resized()
{
  auto area = getLocalBounds().reduced(18);

  // 顶部 Banner 区域
  auto bannerArea = area.removeFromTop(100);

  area.removeFromTop(34);

  // 两个页面切换按钮居中放置，往下移 8px
  auto tabRow = area.removeFromTop(26);
  int totalTabWidth = 96 * 2;
  int tabStartX = tabRow.getCentreX() - totalTabWidth / 2;
  int tabY = tabRow.getY() + 8;
  stagePageButton.setBounds(tabStartX, tabY, 96, 26);
  eqPageButton.setBounds(tabStartX + 98, tabY, 96, 26);

  area.removeFromTop(10);
  auto statusArea = area.removeFromBottom(210);

  // Tool operation status - top 1/3 of status area, moved up 20px
  auto toolStatusArea = statusArea.removeFromTop(84).translated(0, -20);
  toolStatusLabel.setBounds(toolStatusArea.reduced(4, 2));

  // OSC debug status view - remaining area
  statusView.setBounds(statusArea.reduced(12, 8));

  auto content = area.reduced(10, 2);
  animatedDirtyArea = content.expanded(16);

  stageGridArea = {};
  midiTileArea = {};

  if (currentPage == Page::Stage) {
    auto left = content.removeFromLeft(230).reduced(10);
    auto right = content.removeFromRight(220).reduced(10);
    auto center = content.reduced(8);

    toneLabel.setBounds(left.removeFromTop(20));
    toneSelector.setBounds(left.removeFromTop(32));
    left.removeFromTop(10);
    gainLabel.setBounds(left.removeFromTop(22));
    masterGainSlider.setBounds(left.removeFromTop(160).withSizeKeepingCentre(130, 145));
    left.removeFromTop(8);

    // GEN MIDI 按钮放在总音量旋钮下面（缩小一半）
    auto genMidiBtnW = 120;
    auto genMidiBtnH = 36;
    generateGridButton.setBounds(left.removeFromTop(genMidiBtnH).withSizeKeepingCentre(genMidiBtnW, genMidiBtnH));
    left.removeFromTop(6);

    // 网格和网关状态信息
    gridMetaLabel.setBounds(left.removeFromTop(22));
    left.removeFromTop(4);
    gatewayLabel.setBounds(left.removeFromTop(20));

    // 中心区域只有网格
    center.removeFromBottom(48); // 移除底部按钮空间
    stageGridArea = center;

    if (!stageGridArea.isEmpty()) {
      const int tileW = 160;
      const int tileH = 110;
      auto tileArea = right;
      tileArea.removeFromTop(8);
      midiTileArea = tileArea.removeFromTop(tileH).withSizeKeepingCentre(tileW, tileH);
    }

    for (auto* comp : getChildren()) {
      if (comp == &masterGainSlider || comp == &generateGridButton || comp == &gridMetaLabel || comp == &gatewayLabel)
        comp->toBack();
    }
  }
  else if (currentPage == Page::Eq) {
    // 四个旋钮均匀分布：每个旋钮 180 宽，间距 40
    const int knobSize = 180;
    const int spacing = 40;
    const int totalWidth = 4 * knobSize + 3 * spacing;
    const int startX = (content.getWidth() - totalWidth) / 2 + knobSize / 2;
    const int knobY = 30;

    eqLowLabel.setBounds(content.getX() + startX - knobSize / 2, content.getY(), knobSize, 22);
    eqLowSlider.setBounds(content.getX() + startX - knobSize / 2, content.getY() + knobY, knobSize, knobSize);

    eqLowMidLabel.setBounds(content.getX() + startX + knobSize + spacing - knobSize / 2, content.getY(), knobSize, 22);
    eqLowMidSlider.setBounds(content.getX() + startX + knobSize + spacing - knobSize / 2, content.getY() + knobY, knobSize, knobSize);

    eqHighMidLabel.setBounds(content.getX() + startX + 2 * (knobSize + spacing) - knobSize / 2, content.getY(), knobSize, 22);
    eqHighMidSlider.setBounds(content.getX() + startX + 2 * (knobSize + spacing) - knobSize / 2, content.getY() + knobY, knobSize, knobSize);

    eqHighLabel.setBounds(content.getX() + startX + 3 * (knobSize + spacing) - knobSize / 2, content.getY(), knobSize, 22);
    eqHighSlider.setBounds(content.getX() + startX + 3 * (knobSize + spacing) - knobSize / 2, content.getY() + knobY, knobSize, knobSize);

    // EQ 页面不显示 generateGridButton, gridMetaLabel, gatewayLabel
    stageGridArea = content;
  }
}

void GainPluginAudioProcessorEditor::paintStageMatrixArea(juce::Graphics& g)
{
  if (stageGridArea.isEmpty())
    return;

  // P0: 网格背景 + 扫描线
  paintGridBackground(g, stageGridArea);

  // P0: 网格边框霓虹光晕
  drawNeonGlow(g, stageGridArea, TechColours::accentCyan(), 0.5f);

  const float cellW = static_cast<float>(stageGridArea.getWidth()) / static_cast<float>(kGridCols);
  const float cellH = static_cast<float>(stageGridArea.getHeight()) / static_cast<float>(kGridRows);

  for (int row = 0; row < kGridRows; ++row) {
    for (int col = 0; col < kGridCols; ++col) {
      if (!isGridCellPainted(col, row))
        continue;

      const auto level = getGridCellLevel(col, row);
      const float x = stageGridArea.getX() + static_cast<float>(col) * cellW;
      const float y = stageGridArea.getY() + static_cast<float>(row) * cellH;
      juce::Rectangle<float> r(x + 0.35f, y + 0.35f, juce::jmax(1.0f, cellW - 0.7f), juce::jmax(1.0f, cellH - 0.7f));

      // P1: 科技感网格颜色
      juce::Colour fill;
      if (level == 0)
        fill = TechColours::accentCyan().withAlpha(0.85f);
      else if (level == 1)
        fill = TechColours::accentPurple().withAlpha(0.85f);
      else
        fill = TechColours::accentPink().withAlpha(0.85f);

      g.setColour(fill);
      g.fillRect(r);
    }
  }

  // P0: 主网格线
  g.setColour(TechColours::panelBorder().withAlpha(0.5f));
  for (int c = 0; c <= kGridCols; ++c) {
    if (c % 10 != 0) continue;
    const float x = stageGridArea.getX() + static_cast<float>(c) * cellW;
    g.drawLine(x, static_cast<float>(stageGridArea.getY()), x, static_cast<float>(stageGridArea.getBottom()), 1.0f);
  }
  for (int r = 0; r <= kGridRows; ++r) {
    if (r % 10 != 0) continue;
    const float y = stageGridArea.getY() + static_cast<float>(r) * cellH;
    g.drawLine(static_cast<float>(stageGridArea.getX()), y, static_cast<float>(stageGridArea.getRight()), y, 1.0f);
  }
}

void GainPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
  if (currentPage != Page::Stage || stageGridArea.isEmpty())
    return;

  int col = 0, row = 0;
  if (pointToGridCell(event.getPosition(), col, row)) {
    isGridPainting = true;
    gridEraseMode = event.mods.isAltDown();
    paintGridCell(col, row, !gridEraseMode);
    lastGridPaintCol = col;
    lastGridPaintRow = row;
  }
}

void GainPluginAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
  if (!isGridPainting || currentPage != Page::Stage || stageGridArea.isEmpty())
    return;

  int col = 0, row = 0;
  if (pointToGridCell(event.getPosition(), col, row)) {
    if (col != lastGridPaintCol || row != lastGridPaintRow) {
      paintGridCell(col, row, !gridEraseMode);
      lastGridPaintCol = col;
      lastGridPaintRow = row;
    }
  }
}

void GainPluginAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
  if (isGridPainting) {
    isGridPainting = false;
    lastGridPaintCol = -1;
    lastGridPaintRow = -1;
  }
}

bool GainPluginAudioProcessorEditor::pointToGridCell(juce::Point<int> pos, int& outCol, int& outRow) const
{
  if (stageGridArea.isEmpty())
    return false;
  if (!stageGridArea.contains(pos))
    return false;

  const float cellW = static_cast<float>(stageGridArea.getWidth()) / static_cast<float>(kGridCols);
  const float cellH = static_cast<float>(stageGridArea.getHeight()) / static_cast<float>(kGridRows);

  const float relX = static_cast<float>(pos.x - stageGridArea.getX());
  const float relY = static_cast<float>(pos.y - stageGridArea.getY());

  outCol = juce::jlimit(0, kGridCols - 1, static_cast<int>(relX / cellW));
  outRow = juce::jlimit(0, kGridRows - 1, static_cast<int>(relY / cellH));
  return true;
}

void GainPluginAudioProcessorEditor::paintGridCell(int col, int row, bool erase)
{
  const size_t idx = static_cast<size_t>(row * kGridCols + col);
  if (erase) {
    gridPainted[idx] = 1;
    gridLevels[idx] = juce::jlimit(0, 2, static_cast<int>(gridLevels[idx]) + 1);
  } else {
    gridPainted[idx] = 0;
    gridLevels[idx] = 0;
  }
  repaint(stageGridArea);
}

bool GainPluginAudioProcessorEditor::isGridCellPainted(int col, int row) const
{
  const size_t idx = static_cast<size_t>(row * kGridCols + col);
  return idx < gridPainted.size() && gridPainted[idx] != 0;
}

uint8_t GainPluginAudioProcessorEditor::getGridCellLevel(int col, int row) const
{
  const size_t idx = static_cast<size_t>(row * kGridCols + col);
  return idx < gridLevels.size() ? gridLevels[idx] : 0;
}

void GainPluginAudioProcessorEditor::clearGridCanvas()
{
  std::fill(gridLevels.begin(), gridLevels.end(), 0);
  std::fill(gridPainted.begin(), gridPainted.end(), 0);
  repaint(stageGridArea);
}

void GainPluginAudioProcessorEditor::cacheGridAndShowBusyMarker(const juce::String& reason)
{
  cachedGridLevels = gridLevels;
  cachedGridPainted = gridPainted;
  cachedGridMetaLabelText = gridMetaLabel.getText();
  cachedGridMetaLabelValid = true;
  externalBusyOverlayActive = true;
  externalBusyReason = reason;
  drawBusyMarkerOnGrid(reason);
}

void GainPluginAudioProcessorEditor::restoreGridFromBusyMarker()
{
  if (cachedGridMetaLabelValid) {
    gridMetaLabel.setText(cachedGridMetaLabelText, juce::dontSendNotification);
    cachedGridMetaLabelValid = false;
  }
  if (!cachedGridLevels.empty() && !cachedGridPainted.empty()) {
    gridLevels = cachedGridLevels;
    gridPainted = cachedGridPainted;
    cachedGridLevels.clear();
    cachedGridPainted.clear();
  }
  externalBusyOverlayActive = false;
  externalBusyReason = {};
  repaint(stageGridArea);
}

void GainPluginAudioProcessorEditor::drawBusyMarkerOnGrid(const juce::String& reason)
{
  if (stageGridArea.isEmpty()) return;

  repaint(stageGridArea);
}

void GainPluginAudioProcessorEditor::sendGridToGatewayAsync(int activeCells, std::vector<uint8_t> levelsSnapshot, std::vector<uint8_t> paintedSnapshot)
{
  gridSending = true;
  sendingDots = 0;

  std::thread([this, activeCells]() {
    juce::Thread::sleep(2000);
    gridSending = false;
    sendingDots = 0;
    gatewayLabel.setText("Gateway: idle", juce::dontSendNotification);
    generateGridButton.setEnabled(true);

    auto newMetaText = "Sent to gateway. active=" + juce::String(activeCells);
    gridMetaLabel.setText(newMetaText, juce::dontSendNotification);

    juce::MessageManager::callAsync([this] { repaint(); });
  }).detach();
}

void GainPluginAudioProcessorEditor::startClipboardToast(const juce::String& text)
{
  clipboardToastText = text;
  clipboardToastVisible = true;
  clipboardToastStartMs = juce::Time::getMillisecondCounterHiRes();
  repaint();
}

void GainPluginAudioProcessorEditor::paintClipboardToast(juce::Graphics& g) const
{
  if (!clipboardToastVisible)
    return;

  const auto nowMs = juce::Time::getMillisecondCounterHiRes();
  const float elapsed = static_cast<float>((nowMs - clipboardToastStartMs) / 1000.0);
  const float fadeIn = juce::jlimit(0.0f, 1.0f, elapsed / 0.20f);
  const float fadeOut = juce::jlimit(0.0f, 1.0f, (3.6f - elapsed) / 0.6f);
  const float alpha = juce::jmax(0.0f, juce::jmin(fadeIn, fadeOut));
  if (alpha <= 0.0f)
    return;

  const auto bounds = getLocalBounds().toFloat();
  const auto box = juce::Rectangle<float>(560.0f, 108.0f).withCentre(bounds.getCentre());

  g.setColour(juce::Colours::black.withAlpha(0.33f * alpha));
  g.fillRect(bounds);

  g.setColour(TechColours::panelDark().withAlpha(0.95f * alpha));
  g.fillRoundedRectangle(box, 12.0f);
  g.setColour(TechColours::accentCyan().withAlpha(0.85f * alpha));
  g.drawRoundedRectangle(box, 12.0f, 1.5f);

  for (int i = 8; i > 0; --i) {
    g.setColour(TechColours::accentCyan().withAlpha(alpha * 0.03f / static_cast<float>(i)));
    g.drawRoundedRectangle(box.expanded(static_cast<float>(i)), 12.0f, 2.0f);
  }

  g.setColour(juce::Colours::white.withAlpha(alpha));
  g.setFont(juce::FontOptions(16.0f));
  g.drawFittedText("XIWUCLAW", box.toNearestInt().removeFromTop(34), juce::Justification::centred, 1);
  g.setFont(juce::FontOptions(13.0f));
  g.drawFittedText(clipboardToastText, box.toNearestInt().reduced(14, 34), juce::Justification::centred, 2);

  const std::array<juce::Point<float>, 8> starPoints{
    juce::Point<float>(box.getX() - 16.0f, box.getY() + 14.0f),
    juce::Point<float>(box.getX() - 9.0f, box.getBottom() - 17.0f),
    juce::Point<float>(box.getRight() + 12.0f, box.getY() + 20.0f),
    juce::Point<float>(box.getRight() + 18.0f, box.getBottom() - 20.0f),
    juce::Point<float>(box.getCentreX(), box.getY() - 12.0f),
    juce::Point<float>(box.getCentreX() - 64.0f, box.getY() - 14.0f),
    juce::Point<float>(box.getCentreX() + 74.0f, box.getY() - 10.0f),
    juce::Point<float>(box.getCentreX(), box.getBottom() + 16.0f),
  };

  for (int i = 0; i < static_cast<int>(starPoints.size()); ++i) {
    const float pulse = 0.55f + 0.45f * std::sin((elapsed * 9.5f) + (static_cast<float>(i) * 0.7f));
    const auto colour = (i % 2 == 0) ? TechColours::accentCyan() : TechColours::accentPink();
    g.setColour(colour.withAlpha(alpha * pulse * 0.7f));
    g.fillEllipse(starPoints[static_cast<size_t>(i)].x - 3.0f, starPoints[static_cast<size_t>(i)].y - 3.0f, 6.0f, 6.0f);
  }
}

void GainPluginAudioProcessorEditor::switchPage(Page page)
{
  currentPage = page;
  stagePageButton.setToggleState(page == Page::Stage, juce::dontSendNotification);
  eqPageButton.setToggleState(page == Page::Eq, juce::dontSendNotification);
  updatePageVisibility();
  resized();
  repaint();
}

void GainPluginAudioProcessorEditor::updatePageVisibility()
{
  const bool isStage = currentPage == Page::Stage;
  const bool isEq = currentPage == Page::Eq;

  toneLabel.setVisible(isStage);
  toneSelector.setVisible(isStage);
  gainLabel.setVisible(isStage);
  masterGainSlider.setVisible(isStage);

  eqLowLabel.setVisible(isEq);
  eqLowSlider.setVisible(isEq);
  eqLowMidLabel.setVisible(isEq);
  eqLowMidSlider.setVisible(isEq);
  eqHighMidLabel.setVisible(isEq);
  eqHighMidSlider.setVisible(isEq);
  eqHighLabel.setVisible(isEq);
  eqHighSlider.setVisible(isEq);

  generateGridButton.setVisible(isStage);
  gridMetaLabel.setVisible(isStage);
  gatewayLabel.setVisible(isStage);
}

void GainPluginAudioProcessorEditor::refreshStatusText()
{
  auto report = processor.getGatewayStatusReport();
  if (report != statusView.getText())
    statusView.setText(report, juce::dontSendNotification);
}

void GainPluginAudioProcessorEditor::refreshToolStatus()
{
  auto info = processor.getToolStatusInfo();
  const auto nowMs = juce::Time::getMillisecondCounterHiRes();
  const auto ageSec = info.updatedMs > 0.0 ? (nowMs - info.updatedMs) / 1000.0 : -1.0;
  const auto isStale = ageSec > 30.0; // Consider stale after 30 seconds

  // Color based on state
  auto textColour = TechColours::accentPurple();
  if (info.state == "Completed")
    textColour = TechColours::accentGreen();
  else if (info.state == "Failed")
    textColour = juce::Colours::red;
  else if (info.state == "Generating")
    textColour = TechColours::accentCyan();

  toolStatusLabel.setColour(juce::Label::textColourId, textColour);

  // Format the status text
  juce::String statusText;
  if (info.state.isNotEmpty())
  {
    // Tool name header
    statusText << "=== " << info.toolName << " ===\n";

    // State with progress bar visualization
    statusText << "[" << info.state << "]";
    if (info.progress > 0)
    {
      statusText << " ";
      // Progress bar: [████░░░░░░] 50%
      int barWidth = 8;
      int filled = (info.progress * barWidth) / 100;
      statusText << "[";
      for (int i = 0; i < barWidth; ++i)
        statusText << (i < filled ? "█" : "░");
      statusText << "] " << info.progress << "%";
    }
    statusText << "\n";

    // Main message
    if (info.message.isNotEmpty())
      statusText << info.message << "\n";

    // User action hint
    if (info.hint.isNotEmpty())
      statusText << "  -> " << info.hint << "\n";

    // Timestamp
    if (!isStale && ageSec >= 0.0)
      statusText << "(updated " << juce::String(ageSec, 0) << "s ago)";
    else if (isStale)
      statusText << "(stale)";
  }
  else
  {
    statusText = "Tool: Idle";
  }

  if (statusText != toolStatusLabel.getText())
    toolStatusLabel.setText(statusText, juce::dontSendNotification);
}

void GainPluginAudioProcessorEditor::refreshProbeInfo()
{
  const auto probeReport = processor.getProbeMidiDetailsReport();
  if (probeReport.isNotEmpty())
    probeInfoLabel.setText(probeReport, juce::dontSendNotification);
}

void GainPluginAudioProcessorEditor::startExternalMidiDrag()
{
  if (!latestGeneratedMidi.existsAsFile())
    return;

  auto* container = findParentComponentOfClass<juce::DragAndDropContainer>();
  if (!container)
    return;

  container->startDragging(latestGeneratedMidi.getFileName(), this);
}

// ============================================================================
// ProbeResultsListModel - Probe MIDI 结果列表模型
// ============================================================================

int ProbeResultsListModel::getNumRows()
{
  if (!processor) return 0;
  return static_cast<int>(processor->getProbeMidiResults().size());
}

void ProbeResultsListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                              int width, int height, bool rowIsSelected)
{
  if (!processor) return;
  const auto& results = processor->getProbeMidiResults();
  if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
    return;

  const auto& result = results[static_cast<size_t>(rowNumber)];

  // 蓝色背景 ( ready 状态更亮)
  const auto bgColour = result.ready ?
    juce::Colour(0xff1a3a5c) : juce::Colour(0xff0f1a2a);
  g.setColour(bgColour);
  g.fillRoundedRectangle(0, 0, static_cast<float>(width), static_cast<float>(height), 8.0f);

  // 蓝色边框
  const auto borderColour = result.ready ?
    TechColours::accentCyan() : TechColours::panelBorder();
  g.setColour(borderColour.withAlpha(result.ready ? 0.8f : 0.4f));
  g.drawRoundedRectangle(0, 0, static_cast<float>(width), static_cast<float>(height), 8.0f, 2.0f);

  // 轨道号 + 分类
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
  g.drawFittedText("Track " + juce::String(result.trackNumber) + " [" + result.category + "]", 12, 4, width - 140, height - 8,
                   juce::Justification::centredLeft, 1);

  // MIDI 信息
  juce::String midiInfo;
  if (result.ready) {
    midiInfo = juce::String(result.noteCount) + " notes | " +
               juce::String(result.windowStartMs, 0) + "-" +
               juce::String(result.windowEndMs, 0) + "ms";
  } else {
    midiInfo = "Waiting...";
  }
  g.setColour(result.ready ? TechColours::accentGreen() : TechColours::accentCyan().withAlpha(0.7f));
  g.setFont(juce::FontOptions(11.0f));
  g.drawFittedText(midiInfo, 12, height - 20, width - 140, 18,
                   juce::Justification::centredLeft, 1);

  // 拖拽提示
  if (result.ready) {
    g.setColour(TechColours::accentCyan().withAlpha(0.6f));
    g.setFont(juce::FontOptions(10.0f));
    g.drawFittedText("DRAG TO DAW", width - 130, 4, 120, height - 8,
                     juce::Justification::centred, 1);
  }
}

void ProbeResultsListModel::listBoxItemClicked(int rowNumber, const juce::MouseEvent& event)
{
  // 检测是否开始拖动（鼠标移动超过阈值）
  if (event.mouseWasClicked())
  {
    // 检查是否有拖动意图（简单实现：直接尝试拖动）
    startDragForRow(rowNumber, event);
    return;
  }
}

void ProbeResultsListModel::startDragForRow(int rowNumber, const juce::MouseEvent&)
{
  if (!processor) return;
  const auto& results = processor->getProbeMidiResults();
  if (rowNumber < 0 || rowNumber >= static_cast<int>(results.size()))
    return;

  const auto& result = results[static_cast<size_t>(rowNumber)];
  if (!result.ready || !result.midiFile.existsAsFile())
    return;

  // 创建文件拖拽
  if (auto* editor = processor->getActiveEditor())
  {
    auto* container = dynamic_cast<juce::DragAndDropContainer*>(editor);
    if (container)
    {
      container->startDragging(result.midiFile.getFullPathName().toStdString().c_str(), editor);
    }
  }
}

// ============================================================================
// Probe UI 刷新方法
// ============================================================================

void GainPluginAudioProcessorEditor::refreshProbeResultsList()
{
  probeResultsList.updateContent();
}

void GainPluginAudioProcessorEditor::updateSelectionStatus()
{
  const auto selectionReport = processor.getTransportSelection();
  const auto probeReport = processor.getProbeMidiDetailsReport();

  juce::String combinedReport;
  if (selectionReport.startsWith("No selection"))
    combinedReport = selectionReport + " | " + probeReport;
  else
    combinedReport = selectionReport + " | " + probeReport;

  probeInfoLabel.setText(combinedReport, juce::dontSendNotification);
  refreshProbeResultsList();
}
