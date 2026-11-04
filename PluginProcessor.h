#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace AlphaPresets
{
constexpr uint32_t kMagic = 0x48504C41u;

struct FactoryPreset
{
    juce::String name;
    std::vector<uint8_t> payload;
};

inline juce::File makeRoot(const juce::String& productFolder)
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile("Application Support");
   #elif JUCE_WINDOWS
    base = base.getChildFile("Roaming");
   #endif
    return base.getChildFile("Alpha").getChildFile(productFolder);
}

class Manager
{
public:
    Manager(const juce::String& productFolder, uint32_t kind, uint32_t version)
        : root_(makeRoot(productFolder)), kind_(kind), version_(version)
    {
        refreshUserPresets();
    }

    void setFactoryPresets(std::vector<FactoryPreset> presets)
    {
        factory_ = std::move(presets);
    }

    const std::vector<FactoryPreset>& factoryPresets() const noexcept { return factory_; }

    juce::StringArray allProgramNames() const
    {
        juce::StringArray names;
        for (const auto& p : factory_) names.add(p.name);
        for (const auto& f : userFiles_) names.add(f.getFileNameWithoutExtension());
        return names;
    }

    int getNumPrograms() const
    {
        return static_cast<int>(factory_.size() + userFiles_.size());
    }

    void refreshUserPresets()
    {
        userFiles_.clearQuick();
        if (! root_.isDirectory()) return;
        for (const auto& e : juce::RangedDirectoryIterator(
                 root_, false, "*.alph", juce::File::findFiles))
            userFiles_.add(e.getFile());

        std::sort(userFiles_.begin(), userFiles_.end(),
                  [](const juce::File& a, const juce::File& b) {
                      return a.getFileNameWithoutExtension()
                              .compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
                  });
    }

    bool loadUserPreset(int userIndex, std::vector<uint8_t>& payloadOut, juce::String& err) const
    {
        if (userIndex < 0 || userIndex >= userFiles_.size())
        { err = "Preset index out of range"; return false; }

        juce::MemoryBlock mb;
        if (! userFiles_[userIndex].loadFileAsData(mb))
        { err = "Could not read preset"; return false; }

        const uint8_t* payload = nullptr;
        size_t len = 0;
        if (! readEnvelope(static_cast<const uint8_t*>(mb.getData()),
                           static_cast<size_t>(mb.getSize()), payload, len))
        { err = "Not a valid Alpha preset"; return false; }

        payloadOut.assign(payload, payload + len);
        return true;
    }

    bool saveUserPreset(const juce::String& name,
                        const std::vector<uint8_t>& payload,
                        juce::String& err)
    {
        if (name.trim().isEmpty()) { err = "Preset name is empty"; return false; }
        auto d = root_.createDirectory();
        if (d.failed()) { err = d.getErrorMessage(); return false; }

        auto file = root_.getChildFile(juce::File::createLegalFileName(name))
                        .withFileExtension(".alph");
        std::vector<uint8_t> env;
        writeEnvelope(env, payload.data(), payload.size());
        if (! file.replaceWithData(env.data(), env.size()))
        { err = "Could not write " + file.getFullPathName(); return false; }
        refreshUserPresets();
        return true;
    }

    juce::File presetFolder() const { return root_; }

private:
    juce::File root_;
    uint32_t kind_ = 1;
    uint32_t version_ = 1;
    std::vector<FactoryPreset> factory_;
    juce::Array<juce::File> userFiles_;

    void writeEnvelope(std::vector<uint8_t>& out, const uint8_t* payload, size_t len) const
    {
        auto put = [&out](uint32_t v) {
            out.push_back(static_cast<uint8_t>(v));
            out.push_back(static_cast<uint8_t>(v >> 8));
            out.push_back(static_cast<uint8_t>(v >> 16));
            out.push_back(static_cast<uint8_t>(v >> 24));
        };
        put(kMagic); put(kind_); put(version_); put(static_cast<uint32_t>(len));
        out.insert(out.end(), payload, payload + len);
    }

    bool readEnvelope(const uint8_t* data, size_t n,
                      const uint8_t*& payload, size_t& len) const
    {
        if (data == nullptr || n < 16) return false;
        auto get = [&](size_t at) {
            return static_cast<uint32_t>(data[at])
                 | (static_cast<uint32_t>(data[at+1]) << 8)
                 | (static_cast<uint32_t>(data[at+2]) << 16)
                 | (static_cast<uint32_t>(data[at+3]) << 24);
        };
        if (get(0) != kMagic) return false;
        if (get(4) != kind_) return false;
        if (get(8) != version_) return false;
        const uint32_t l = get(12);
        if (n < 16 + l) return false;
        payload = data + 16;
        len = l;
        return true;
    }
};

} // namespace AlphaPresets

class IEffect
{
public:
    virtual ~IEffect() = default;
    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void process(float* left, float* right, int n) = 0;
    virtual void reset() = 0;
    virtual void setTempo(double) {}
    virtual void setTransport(bool, double) {}
    virtual int  latency() const { return 0; }
    virtual bool wantsKeyInput() const { return false; }
    virtual bool wantsExtraBus()  const { return false; }
    virtual const char* name() const = 0;
    virtual void getState(std::vector<uint8_t>& out) const = 0;
    virtual bool setState(const uint8_t* data, size_t size) = 0;
};

namespace alpha::dsp
{
constexpr float TINY = 5.9604645e-8f;

inline float s(double x) { return static_cast<float>(x); }
inline int64_t round_ties_even(double x)
{
    const double f = std::floor(x);
    const double d = x - f;
    if (d > 0.5) return static_cast<int64_t>(f + 1.0);
    if (d < 0.5) return static_cast<int64_t>(f);
    return (std::fmod(f, 2.0) == 0.0) ? static_cast<int64_t>(f) : static_cast<int64_t>(f + 1.0);
}
inline int64_t trunc_toward_zero(double x) { return static_cast<int64_t>(x); }
inline float flush(float x) { return std::fabs(x) < TINY ? 0.0f : x; }
inline double flush_le(double x) { return std::fabs(x) <= static_cast<double>(TINY) ? 0.0 : x; }

struct Expo
{
    float c0 = 0, c1 = 0, c2 = 0;
    static inline Expo make(float v)
    {
        if (v == 0.0f) return {};
        const double a = std::fabs(static_cast<double>(v));
        return { v, s(1.0 / a), s(std::log(a + 1.0)) };
    }
    double eval(double u) const
    {
        if (c0 == 0.0f) return u;
        if (c0 > 0.0f) return 1.0 - ((std::exp((1.0 - u) * static_cast<double>(c2)) - 1.0) * static_cast<double>(c1));
        return (std::exp(u * static_cast<double>(c2)) - 1.0) * static_cast<double>(c1);
    }
};

class Reader
{
public:
    explicit Reader(const uint8_t* data, size_t size) : b_(data), n_(size), at_(0) {}
    bool ok() const { return !bad_; }
    size_t left() const { return n_ - at_; }
    bool bytes(size_t n, const uint8_t*& out)
    {
        if (bad_ || at_ + n > n_) { bad_ = true; return false; }
        out = b_ + at_; at_ += n; return true;
    }
    bool u8(uint8_t& v)
    {
        const uint8_t* p; if (!bytes(1, p)) return false;
        v = p[0]; return true;
    }
    bool i32(int32_t& v)
    {
        const uint8_t* p; if (!bytes(4, p)) return false;
        v = static_cast<int32_t>(static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2]) << 16)
            | (static_cast<uint32_t>(p[3]) << 24));
        return true;
    }
    bool f32(float& v)
    {
        int32_t i; if (!i32(i)) return false;
        std::memcpy(&v, &i, 4); return true;
    }
    bool f64(double& v)
    {
        const uint8_t* p; if (!bytes(8, p)) return false;
        uint64_t u = 0;
        for (int k = 0; k < 8; ++k) u |= static_cast<uint64_t>(p[k]) << (8 * k);
        std::memcpy(&v, &u, 8); return true;
    }
    bool ints(size_t n, std::vector<int32_t>& v)
    {
        v.resize(n);
        for (size_t k = 0; k < n; ++k) if (!i32(v[k])) return false;
        return true;
    }
private:
    const uint8_t* b_; size_t n_; size_t at_; bool bad_ = false;
};

constexpr float R12800 = 1.0f / 12800.0f;
constexpr float R6400  = 1.0f / 6400.0f;
constexpr float R65536 = 1.0f / 65536.0f;

inline float knob(int32_t value, float recip) { return static_cast<float>(value) * recip; }

inline float curve(float x)
{
    if (x == 1.0f) return 1.0f;
    const float ln11 = static_cast<float>(std::log(11.0));
    const float p = x * ln11;
    return static_cast<float>((std::exp(static_cast<double>(p)) - 1.0) * static_cast<double>(0.1f));
}

inline void pan_split(float level, float pan, float& l, float& r)
{
    const float p = std::fmax(-1.0f, std::fmin(1.0f, pan));
    const double a = (static_cast<double>(p) + 1.0) * (std::acos(-1.0) / 4.0);
    const double L = static_cast<double>(level);
    l = static_cast<float>(L * std::cos(a));
    r = static_cast<float>(L * std::sin(a));
}

inline float unity_at_centre(float level)
{
    return static_cast<float>(static_cast<double>(level) * std::sqrt(2.0));
}

inline float engine_limit(uint32_t rate)
{
    const float r = (44100.0f / static_cast<float>(rate < 1 ? 1 : rate));
    return static_cast<float>(static_cast<double>(r) * 0.001);
}

inline float plugin_limit(uint32_t rate)
{
    return static_cast<float>(44100.0 / static_cast<double>(rate < 1 ? 1 : rate) * 0.001);
}

struct Ramp
{
    float value = 0.0f;
    float step  = 0.0f;
    inline float next() { const float v = value; value += step; return v; }
    inline bool  is_flat() const { return step == 0.0f; }
    inline float first()  const { return value; }
    inline float step_v() const { return step; }
};

struct Glide
{
    float current = 0.0f;
    explicit Glide(float v = 0.0f) : current(v) {}
    inline Ramp block(float target, int n, float limit)
    {
        const float start = current;
        if (start == target) return { start, 0.0f };
        const float nf = static_cast<float>(n < 1 ? 1 : n);
        float step = (target - start) / nf;
        float end;
        if (std::fabs(step) <= limit)
        {
            end = target;
        }
        else
        {
            step = std::copysign(limit, target - start);
            float e = start + step * nf;
            if (std::fabs(e) < 1.0f / 16777216.0f) { step = -start / nf; e = 0.0f; }
            end = e;
        }
        current = end;
        return { start, step };
    }
};

} // namespace alpha::dsp

class AlphaBalanceEffect final : public IEffect
{
public:
    enum { BALANCE = 0, VOLUME = 1, NUM = 2 };
    static constexpr std::array<int, NUM> kDefaults { 0, 256 };
    static constexpr std::array<std::pair<int,int>, NUM> kRanges {{ {-128,128}, {0,320} }};
    static constexpr int kStateLen = 8;

    explicit AlphaBalanceEffect(double sampleRate = 44100.0)
    {
        const auto g = gains(kDefaults[0], kDefaults[1]);
        target_ = g;
        for (int i = 0; i < 4; ++i) glides_[i] = alpha::dsp::Glide(g[i]);
        limit_ = limit(sampleRate);
    }

    void prepare(double sr, int) override { limit_ = limit(sr); }
    void reset() override { flush(); }

    void process(float* l, float* r, int n) override
    {
        alpha::dsp::Ramp ramps[4];
        for (int i = 0; i < 4; ++i) ramps[i] = glides_[i].block(target_[i], n, limit_);
        for (int i = 0; i < n; ++i)
        {
            const float x = l[i], y = r[i];
            const float a = ramps[0].next(), b = ramps[1].next(), c = ramps[2].next(), d = ramps[3].next();
            l[i] = a * x + c * y;
            r[i] = b * y + d * x;
        }
    }

    const char* name() const override { return "Alpha Balance"; }

    void getState(std::vector<uint8_t>& out) const override
    {
        out.clear();
        for (int v : applied_)
        {
            out.push_back(static_cast<uint8_t>(v));
            out.push_back(static_cast<uint8_t>(v >> 8));
            out.push_back(static_cast<uint8_t>(v >> 16));
            out.push_back(static_cast<uint8_t>(v >> 24));
        }
    }

    bool setState(const uint8_t* d, size_t n) override
    {
        if (n < kStateLen) return false;
        std::array<int, NUM> v{};
        for (int i = 0; i < NUM; ++i)
        {
            const size_t at = 4 * static_cast<size_t>(i);
            v[i] = static_cast<int32_t>(static_cast<uint32_t>(d[at])
                | (static_cast<uint32_t>(d[at+1]) << 8)
                | (static_cast<uint32_t>(d[at+2]) << 16)
                | (static_cast<uint32_t>(d[at+3]) << 24));
        }
        applied_ = v;
        target_ = gains(v[BALANCE], v[VOLUME]);
        return true;
    }

    void setSettings(const std::array<int, NUM>& v)
    {
        if (v == applied_) return;
        applied_ = v;
        target_ = gains(v[BALANCE], v[VOLUME]);
    }

    const std::array<int, NUM>& settings() const { return applied_; }

    void flush()
    {
        for (int i = 0; i < 4; ++i) glides_[i] = alpha::dsp::Glide(target_[i]);
    }

    static inline float limit(double sampleRate)
    {
        return alpha::dsp::plugin_limit(static_cast<uint32_t>(sampleRate < 1.0 ? 1.0 : sampleRate)) * 10.0f;
    }

    static inline float levelFromVolume(int volume)
    {
        const float x = static_cast<float>(static_cast<double>(volume) / 320.0 * 1.25);
        return static_cast<float>(std::exp(static_cast<double>(x) * std::log(11.0)) - 1.0);
    }

    static inline std::array<float, 4> gains(int balance, int volume)
    {
        const double PI_D = 3.14159265358979323846;
        struct Dd { double hi, lo; };
        auto dd_quick = [](double a, double b) { const double s = a + b; return Dd{ s, b - (s - a) }; };
        auto dd_sum   = [](double a, double b) { const double s = a + b; const double bb = s - a; return Dd{ s, (a - (s - bb)) + (b - bb) }; };
        auto dd_prod  = [](double a, double b) { const double p = a * b; return Dd{ p, std::fma(a, b, -p) }; };
        auto dd_neg   = [](Dd x) { return Dd{ -x.hi, -x.lo }; };
        auto dd_add   = [&](Dd x, Dd y) { const Dd s = dd_sum(x.hi, y.hi); return dd_quick(s.hi, s.lo + x.lo + y.lo); };
        auto dd_mul   = [&](Dd x, Dd y) { const Dd p = dd_prod(x.hi, y.hi); return dd_quick(p.hi, p.lo + (x.hi * y.lo + x.lo * y.hi)); };
        auto dd_div   = [&](Dd x, double d) { const double q = x.hi / d; const Dd r = dd_add(x, dd_neg(dd_prod(q, d))); return dd_quick(q, r.hi / d); };
        auto series   = [&](double x, Dd first, uint32_t n) {
            const Dd x2 = dd_prod(x, x);
            Dd term = first, sum = first;
            while (std::fabs(term.hi) > 1e-40 * std::fmax(std::fabs(sum.hi), 1e-300))
            {
                const double d = static_cast<double>((n + 1) * (n + 2));
                term = dd_neg(dd_div(dd_mul(term, x2), d));
                sum = dd_add(sum, term);
                n += 2;
            }
            return sum.hi + sum.lo;
        };
        auto dd_sin = [&](double x) { return series(x, { x, 0.0 }, 1); };
        auto dd_cos = [&](double x) { return series(x, { 1.0, 0.0 }, 0); };

        const float level = levelFromVolume(volume);
        const double b = static_cast<double>(balance);
        const double side = std::fabs(b) / 256.0;
        const double unit = 1.0 / (10.0 * dd_cos(PI_D / 4.0));
        const double g = static_cast<double>(level) * (1.0 - 0.5 * dd_sin(side * PI_D)) * unit;
        const double angle = (b / 128.0 + 1.0) * PI_D / 4.0;
        const double ll = g * dd_cos(angle);
        const double rr = g * dd_sin(angle);
        double rl = 0.0, lr = 0.0;
        if (balance < 0) rl = ll - rr;
        else             lr = rr - ll;
        return { static_cast<float>(ll), static_cast<float>(rr), static_cast<float>(rl), static_cast<float>(lr) };
    }

private:
    std::array<int, NUM> applied_ = kDefaults;
    std::array<float, 4> target_{ 1, 1, 0, 0 };
    std::array<alpha::dsp::Glide, 4> glides_;
    float limit_ = 0.0f;
};

class AlphaDelayProcessor : public juce::AudioProcessor
{
public:
    AlphaDelayProcessor();
    ~AlphaDelayProcessor() override = default;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Alpha Balance"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    AlphaPresets::Manager presets;
    AlphaBalanceEffect balance;

    bool saveCurrentAsUserPreset(const juce::String& name, juce::String& err);
    void resetEffectToTarget();

    juce::AudioParameterInt* balanceParam = nullptr;
    juce::AudioParameterInt* volumeParam  = nullptr;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void pushParametersToDsp();
    void seedDefaults();

    int currentProgram = 0;
    std::atomic<float>* rawBalance = nullptr;
    std::atomic<float>* rawVolume  = nullptr;
    std::atomic<int> lastBalance { 0 };
    std::atomic<int> lastVolume  { 256 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaDelayProcessor)
};