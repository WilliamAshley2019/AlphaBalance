#include "PluginProcessor.h"

namespace
{
std::vector<uint8_t> serializeParams(juce::AudioProcessorValueTreeState& apvts)
{
    std::vector<uint8_t> out;
    auto state = apvts.copyState();
    auto xml = state.createXml();
    if (xml != nullptr)
    {
        const auto text = xml->toString();
        out.assign(text.toRawUTF8(), text.toRawUTF8() + text.getNumBytesAsUTF8());
    }
    return out;
}

bool deserializeParams(juce::AudioProcessorValueTreeState& apvts,
                       const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0) return false;
    const auto text = juce::String::fromUTF8(reinterpret_cast<const char*>(data), static_cast<int>(len));
    auto xml = juce::parseXML(text);
    if (xml == nullptr) return false;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}
} // namespace

AlphaDelayProcessor::AlphaDelayProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", makeLayout()),
      presets("Alpha Balance", 1, 1)
{
    balanceParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("balance"));
    volumeParam  = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("volume"));

    rawBalance = apvts.getRawParameterValue("balance");
    rawVolume  = apvts.getRawParameterValue("volume");

    std::vector<AlphaPresets::FactoryPreset> f;
    f.push_back({ "Init", {} });
    presets.setFactoryPresets(std::move(f));

    pushParametersToDsp();
    balance.flush();
    setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout
AlphaDelayProcessor::makeLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    params.push_back(std::make_unique<AudioParameterInt>(
        "balance", "Balance", -128, 128, 0));
    params.push_back(std::make_unique<AudioParameterInt>(
        "volume", "Volume", 0, 320, 256));
    return { params.begin(), params.end() };
}

void AlphaDelayProcessor::seedDefaults()
{
    if (balanceParam != nullptr)
        balanceParam->setValueNotifyingHost(
            balanceParam->convertTo0to1(
                static_cast<float>(AlphaBalanceEffect::kDefaults[AlphaBalanceEffect::BALANCE])));
    if (volumeParam != nullptr)
        volumeParam->setValueNotifyingHost(
            volumeParam->convertTo0to1(
                static_cast<float>(AlphaBalanceEffect::kDefaults[AlphaBalanceEffect::VOLUME])));
}

void AlphaDelayProcessor::pushParametersToDsp()
{
    const int b = (rawBalance != nullptr) ? juce::roundToInt(rawBalance->load()) : lastBalance.load();
    const int v = (rawVolume  != nullptr) ? juce::roundToInt(rawVolume->load())  : lastVolume.load();
    if (b == lastBalance.load() && v == lastVolume.load()) return;
    lastBalance.store(b);
    lastVolume.store(v);
    balance.setSettings({ b, v });
}

void AlphaDelayProcessor::resetEffectToTarget()
{
    balance.flush();
}

void AlphaDelayProcessor::prepareToPlay(double sr, int block)
{
    balance.prepare(sr, block);
    balance.flush();
}

void AlphaDelayProcessor::releaseResources() { balance.flush(); }

bool AlphaDelayProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono()
                      || in == juce::AudioChannelSet::stereo());
}

void AlphaDelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    pushParametersToDsp();

    const int n  = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();
    if (n == 0 || ch == 0) return;
    if (ch >= 2)
        balance.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
}

int AlphaDelayProcessor::getNumPrograms() { return presets.getNumPrograms(); }
int AlphaDelayProcessor::getCurrentProgram() { return currentProgram; }

void AlphaDelayProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= presets.getNumPrograms()) return;
    currentProgram = index;
    const int fc = static_cast<int>(presets.factoryPresets().size());
    if (index < fc)
    {
        const auto& fp = presets.factoryPresets()[static_cast<size_t>(index)];
        if (! deserializeParams(apvts, fp.payload.data(), fp.payload.size()))
            seedDefaults();
        pushParametersToDsp();
        balance.flush();
        return;
    }
    std::vector<uint8_t> payload;
    juce::String err;
    if (presets.loadUserPreset(index - fc, payload, err))
    {
        if (deserializeParams(apvts, payload.data(), payload.size()))
        {
            pushParametersToDsp();
            balance.flush();
        }
    }
}

const juce::String AlphaDelayProcessor::getProgramName(int index)
{
    const auto names = presets.allProgramNames();
    if (index < 0 || index >= names.size()) return {};
    return names[index];
}

bool AlphaDelayProcessor::saveCurrentAsUserPreset(const juce::String& name,
                                                  juce::String& err)
{
    return presets.saveUserPreset(name, serializeParams(apvts), err);
}

void AlphaDelayProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto payload = serializeParams(apvts);
    destData.replaceAll(payload.data(), payload.size());
}

void AlphaDelayProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (deserializeParams(apvts, static_cast<const uint8_t*>(data), static_cast<size_t>(sizeInBytes)))
    {
        pushParametersToDsp();
        balance.flush();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AlphaDelayProcessor();
}