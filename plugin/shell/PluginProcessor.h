// GoatTracker Ultimate plugin: the audio processor (VST3 / AAX / AU)

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CoreLoader.h"

class GoatTrackerProcessor : public juce::AudioProcessor
{
public:
  GoatTrackerProcessor();
  ~GoatTrackerProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "GoatTracker Ultimate"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "GoatTracker"; }
  void changeProgramName(int, const juce::String &) override {}

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // The core library, started on first use (hosts make instances just to
  // scan them)
  CoreLoader &getCore();
  bool coreReady();

  // The editor's size (logical pixels), saved with the project
  int editorWidth = 0;
  int editorHeight = 0;

  // Called on the GoatTracker thread (see gtcore_callbacks)
  std::function<void(int, int)> onResizeRequest;

private:
  bool startCore(int w, int h, float scale);

  CoreLoader core;
  bool coreTried = false;
  juce::CriticalSection coreLock;
  double currentRate = 44100.0;
  int currentBlock = 512;
  bool prepared = false;
  std::vector<gtcore_midi> midiEvents;
  juce::AudioBuffer<float> scratch;

  // Shared with the callbacks, which may run after this object is gone
  struct Shared
  {
    std::atomic<GoatTrackerProcessor *> owner { nullptr };
  };
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  std::shared_ptr<Shared> *callbackUser = nullptr;

  static void callbackResize(void *user, int w, int h);
  static void callbackDirty(void *user);
  static void callbackClipboard(void *user, const char *text);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GoatTrackerProcessor)
};
