#ifndef AKIRA_INPUT_PAD_PATH_HOS_HPP
#define AKIRA_INPUT_PAD_PATH_HOS_HPP

#include <switch.h>

#include <array>
#include <cstdint>
#include <optional>

namespace akira::input {

class HosPadPath {
public:
    HosPadPath();
    ~HosPadPath();

    HosPadPath(const HosPadPath&) = delete;
    HosPadPath& operator=(const HosPadPath&) = delete;

    void poll();
    HidAnalogStickState stickPos(int index) const;
    std::optional<HidSixAxisSensorState> readGyro();
    void resetMotion();
    void sendRumble(float left, float right, float freqLow, float freqHigh);

    uint64_t heldButtons() const { return m_buttons; }
    bool menuHeld() const { return (m_buttons & HidNpadButton_Minus) != 0; }

private:
    enum Sensor : std::size_t {
        Handheld,
        FullKey,
        JoyDualLeft,
        JoyDualRight,
        SensorCount,
    };

    std::optional<HidSixAxisSensorState> readSensor(Sensor sensor) const;

    PadState m_pad{};
    uint64_t m_buttons = 0;
    std::array<HidSixAxisSensorHandle, SensorCount> m_sixaxis{};
    std::array<bool, SensorCount> m_started{};
};

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_PATH_HOS_HPP
