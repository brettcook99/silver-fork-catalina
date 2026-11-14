// GoatTracker Ultimate plugin: the editor window. It shows the picture the
// GoatTracker core draws (the same retro screen as the standalone program)
// and passes the keys, the mouse and dropped files to it.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class GoatTrackerEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
  explicit GoatTrackerEditor(GoatTrackerProcessor &);
  ~GoatTrackerEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  bool keyPressed(const juce::KeyPress &key) override;
  bool keyStateChanged(bool isKeyDown) override;
  void modifierKeysChanged(const juce::ModifierKeys &mods) override;
  void focusGained(FocusChangeType) override;
  void focusLost(FocusChangeType) override;

  void mouseMove(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseUp(const juce::MouseEvent &) override;
  void mouseExit(const juce::MouseEvent &) override;
  void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;

  bool isInterestedInFileDrag(const juce::StringArray &files) override;
  void filesDropped(const juce::StringArray &files, int x, int y) override;

private:
  void timerCallback() override;
  void send(gtcore_inputevent &in);
  void sendSize();
  float physicalScale();
  void mouseEvent(int type, const juce::MouseEvent &e, int button);

  GoatTrackerProcessor &processor;
  bool coreOk = false;
  juce::Image frame;
  unsigned frameSerial = 0;
  float lastScale = 0.0f;
  float paintScale = 0.0f;
  int lastCursor = -1;
  int buttonsDown = 0;

  struct HeldKey
  {
    int juceCode;
    int sym;
    int scancode;
  };
  std::vector<HeldKey> heldKeys;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoatTrackerEditor)
};
