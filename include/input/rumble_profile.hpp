#ifndef AKIRA_INPUT_RUMBLE_PROFILE_HPP
#define AKIRA_INPUT_RUMBLE_PROFILE_HPP

namespace akira::input {


inline constexpr const char* kRumbleKeyDefault = "default";

inline constexpr const char* kRumbleKeySwitch = "switch";

inline constexpr const char* kRumbleKeyJoyCon    = "joycon";
inline constexpr const char* kRumbleKeySwitchPro = "switchpro";

enum class RumbleSource {
    Off = 0,
    Derived,
    Game,
};

enum class HapticIntensity {
    Off = 0,
    VeryWeak,
    Weak,
    Normal,
    Strong,
    VeryStrong,
};

struct RumbleProfile {
    float strength  = 1.0f;
    float ceiling   = 1.0f;

    bool  per_motor = true;

    float freq_low  = 160.0f;
    float freq_high = 320.0f;

    float envelope_attack = 0.60f;
    float envelope_decay  = 0.85f;

    HapticIntensity haptic_intensity = HapticIntensity::Normal;

    RumbleSource rumble_source = RumbleSource::Derived;
};

inline RumbleProfile DefaultRumbleProfile()
{
    RumbleProfile p;
    p.strength  = 1.0f;
    p.ceiling   = 1.0f;
    p.per_motor = true;
    return p;
}

inline RumbleProfile SwitchRumbleProfile()
{
    RumbleProfile p;
    p.strength  = 1.0f;
    p.ceiling   = 0.63f;
    p.per_motor = false;
    p.freq_low  = 160.0f;
    p.freq_high = 320.0f;
    return p;
}

inline float HapticSampleGain(HapticIntensity intensity)
{
    switch (intensity) {
        case HapticIntensity::Off:        return 0.0f;
        case HapticIntensity::VeryWeak:   return 0.4f;
        case HapticIntensity::Weak:       return 0.7f;
        case HapticIntensity::Normal:     return 1.0f;
        case HapticIntensity::Strong:     return 1.6f;
        case HapticIntensity::VeryStrong: return 2.4f;
    }
    return 1.0f;
}

inline int HapticBaseForIntensity(HapticIntensity intensity)
{
    const float gain = HapticSampleGain(intensity);
    if (gain <= 0.0f)
        return 400;
    return (int)(50.0f / gain);
}

} // namespace akira::input

#endif // AKIRA_INPUT_RUMBLE_PROFILE_HPP
