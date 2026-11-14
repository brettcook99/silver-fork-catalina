#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
  // The plugin's own state around the core's: a header, the editor size,
  // then the core's state (the song and its settings)
  const juce::uint32 stateMagic = 0x50555447; // "GTUP"
  const int stateVersion = 1;
}

GoatTrackerProcessor::GoatTrackerProcessor()
  : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
  shared->owner = this;
  midiEvents.reserve(4096);
}

GoatTrackerProcessor::~GoatTrackerProcessor()
{
  shared->owner = nullptr;
  // GoatTracker stops first: its thread may call back until then
  const bool stopped = core.shutdown();
  if ((stopped) && (callbackUser)) delete callbackUser;
  // Otherwise the library stays loaded and may still call: its user pointer
  // stays too
  callbackUser = nullptr;
}

bool GoatTrackerProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
  const auto &out = layouts.getMainOutputChannelSet();
  if ((out != juce::AudioChannelSet::stereo()) && (out != juce::AudioChannelSet::mono())) return false;
  return layouts.getMainInputChannelSet().isDisabled() || (layouts.inputBuses.size() == 0);
}

void GoatTrackerProcessor::callbackResize(void *user, int w, int h)
{
  auto weak = std::weak_ptr<Shared>(*static_cast<std::shared_ptr<Shared> *>(user));
  juce::MessageManager::callAsync([weak, w, h]
  {
    if (auto s = weak.lock())
      if (auto *p = s->owner.load())
        if (p->onResizeRequest) p->onResizeRequest(w, h);
  });
}

void GoatTrackerProcessor::callbackDirty(void *user)
{
  auto weak = std::weak_ptr<Shared>(*static_cast<std::shared_ptr<Shared> *>(user));
  juce::MessageManager::callAsync([weak]
  {
    if (auto s = weak.lock())
      if (auto *p = s->owner.load())
        p->updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
  });
}

void GoatTrackerProcessor::callbackClipboard(void *, const char *text)
{
  auto copy = juce::String::fromUTF8(text ? text : "");
  juce::MessageManager::callAsync([copy] { juce::SystemClipboard::copyTextToClipboard(copy); });
}

bool GoatTrackerProcessor::startCore(int w, int h, float scale)
{
  const juce::ScopedLock sl(coreLock);
  if (core.isLoaded()) return true;
  if (coreTried) return false;
  coreTried = true;

  // The callbacks' user pointer (the library may call until it has been
  // stopped)
  if (!callbackUser) callbackUser = new std::shared_ptr<Shared>(shared);
  gtcore_callbacks cb {};
  cb.user = callbackUser;
  cb.requestresize = callbackResize;
  cb.markdirty = callbackDirty;
  cb.setclipboard = callbackClipboard;
  cb.filedialog = nullptr;

  if (!core.load(cb, w, h, scale))
  {
    juce::Logger::writeToLog("GoatTracker: " + core.getError());
    return false;
  }
  if (prepared) core.prepare(currentRate, currentBlock);
  return true;
}

CoreLoader &GoatTrackerProcessor::getCore()
{
  return core;
}

bool GoatTrackerProcessor::coreReady()
{
  if (core.isLoaded()) return true;
  int w = editorWidth, h = editorHeight;
  if ((w <= 0) || (h <= 0))
  {
    w = 1200;
    h = 777;
  }
  return startCore(w, h, 1.0f);
}

void GoatTrackerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  currentRate = sampleRate;
  currentBlock = samplesPerBlock;
  prepared = true;
  scratch.setSize(2, juce::jmax(1, samplesPerBlock));
  if (coreReady()) core.prepare(sampleRate, samplesPerBlock);
}

void GoatTrackerProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
  juce::ScopedNoDenormals noDenormals;
  const int samples = buffer.getNumSamples();

  if ((!core.isLoaded()) || (samples <= 0))
  {
    buffer.clear();
    return;
  }

  // The host's transport
  gtcore_transport tp {};
  if (auto *playhead = getPlayHead())
  {
    if (auto pos = playhead->getPosition())
    {
      tp.valid = 1;
      tp.playing = pos->getIsPlaying() ? 1 : 0;
      tp.recording = pos->getIsRecording() ? 1 : 0;
      if (auto bpm = pos->getBpm()) tp.bpm = *bpm;
      else tp.valid = 0;
      if (auto ppq = pos->getPpqPosition()) tp.ppq = *ppq;
      else if (auto secs = pos->getTimeInSeconds()) tp.ppq = *secs * tp.bpm / 60.0;
      tp.looping = pos->getIsLooping() ? 1 : 0;
      if (auto loop = pos->getLoopPoints())
      {
        tp.loopstart = loop->ppqStart;
        tp.loopend = loop->ppqEnd;
      }
      else tp.looping = 0;
      if (auto sig = pos->getTimeSignature())
      {
        tp.timesignum = sig->numerator;
        tp.timesigden = sig->denominator;
      }
    }
  }

  // The block's MIDI (no allocation: the vector has room reserved)
  midiEvents.clear();
  for (const auto meta : midi)
  {
    const auto msg = meta.getMessage();
    if (msg.getRawDataSize() < 2) continue;
    const auto *raw = msg.getRawData();
    if ((raw[0] & 0xf0) < 0x80 || (raw[0] & 0xf0) > 0xb0) continue;
    if (midiEvents.size() >= midiEvents.capacity()) break;
    gtcore_midi e;
    e.offset = juce::jlimit(0, samples - 1, meta.samplePosition);
    e.status = raw[0];
    e.data1 = raw[1] & 0x7f;
    e.data2 = (msg.getRawDataSize() > 2) ? (raw[2] & 0x7f) : 0;
    midiEvents.push_back(e);
  }
  midi.clear();

  if (buffer.getNumChannels() >= 2)
  {
    core.process(buffer.getWritePointer(0), buffer.getWritePointer(1), samples, &tp, midiEvents.data(), (int)midiEvents.size());
    for (int c = 2; c < buffer.getNumChannels(); c++) buffer.clear(c, 0, samples);
  }
  else
  {
    if (scratch.getNumSamples() < samples) scratch.setSize(2, samples, false, false, true);
    core.process(scratch.getWritePointer(0), scratch.getWritePointer(1), samples, &tp, midiEvents.data(), (int)midiEvents.size());
    if (buffer.getNumChannels() == 1) buffer.copyFrom(0, 0, scratch, 0, 0, samples);
  }
}

juce::AudioProcessorEditor *GoatTrackerProcessor::createEditor()
{
  return new GoatTrackerEditor(*this);
}

void GoatTrackerProcessor::getStateInformation(juce::MemoryBlock &destData)
{
  juce::MemoryOutputStream out(destData, false);
  out.writeInt((int)stateMagic);
  out.writeInt(stateVersion);
  out.writeInt(editorWidth);
  out.writeInt(editorHeight);
  void *data = nullptr;
  int size = 0;
  if ((coreReady()) && (core.getstate(&data, &size)) && (data))
  {
    out.writeInt(size);
    out.write(data, (size_t)size);
    core.freedata(data);
  }
  else out.writeInt(0);
}

void GoatTrackerProcessor::setStateInformation(const void *data, int sizeInBytes)
{
  juce::MemoryInputStream in(data, (size_t)sizeInBytes, false);
  if ((sizeInBytes < 20) || ((juce::uint32)in.readInt() != stateMagic)) return;
  const int version = in.readInt();
  if (version < 1) return;
  editorWidth = in.readInt();
  editorHeight = in.readInt();
  const int size = in.readInt();
  if ((size <= 0) || (size > sizeInBytes)) return;
  juce::MemoryBlock block;
  in.readIntoMemoryBlock(block, size);
  if (coreReady()) core.setstate(block.getData(), (int)block.getSize());
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
  return new GoatTrackerProcessor();
}
