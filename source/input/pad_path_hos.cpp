#include "input/pad_path_hos.hpp"

#include "core/settings_manager.hpp"

#include <borealis.hpp>

#include <algorithm>

namespace akira::input {

HosPadPath::HosPadPath()
{
    padInitializeDefault(&m_pad);
    padUpdate(&m_pad);

    hidGetSixAxisSensorHandles(&m_sixaxis[Handheld], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&m_sixaxis[FullKey], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&m_sixaxis[JoyDualLeft], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);

    for (std::size_t i = 0; i < SensorCount; i++) {
        const Result rc = hidStartSixAxisSensor(m_sixaxis[i]);
        m_started[i] = R_SUCCEEDED(rc);
        if (!m_started[i])
            brls::Logger::warning("PadPath: six-axis start failed for sensor {}: 0x{:x}", i, rc);
    }
}

HosPadPath::~HosPadPath()
{
    sendRumble(0.0f, 0.0f, 0.0f, 0.0f);

    for (std::size_t i = 0; i < SensorCount; i++) {
        if (m_started[i])
            hidStopSixAxisSensor(m_sixaxis[i]);
    }
}

void HosPadPath::poll()
{
    padUpdate(&m_pad);
    m_buttons = padGetButtons(&m_pad);
}

HidAnalogStickState HosPadPath::stickPos(int index) const
{
    return padGetStickPos(&m_pad, index);
}

std::optional<HidSixAxisSensorState> HosPadPath::readSensor(Sensor sensor) const
{
    HidSixAxisSensorState state{};
    if (hidGetSixAxisSensorStates(m_sixaxis[sensor], &state, 1) == 0)
        return std::nullopt;
    return state;
}

std::optional<HidSixAxisSensorState> HosPadPath::readGyro()
{
    const uint64_t styleSet = padGetStyleSet(&m_pad);

    if (styleSet & HidNpadStyleTag_NpadHandheld)
        return readSensor(Handheld);

    if (styleSet & HidNpadStyleTag_NpadFullKey)
        return readSensor(FullKey);

    if (!(styleSet & HidNpadStyleTag_NpadJoyDual))
        return std::nullopt;

    const uint64_t attrib = padGetAttributes(&m_pad);
    const bool leftConnected  = attrib & HidNpadAttribute_IsLeftConnected;
    const bool rightConnected = attrib & HidNpadAttribute_IsRightConnected;

    switch (SettingsManager::getInstance()->getGyroSource()) {
        case GyroSource::Left:
            return leftConnected ? readSensor(JoyDualLeft) : std::nullopt;
        case GyroSource::Right:
            return rightConnected ? readSensor(JoyDualRight) : std::nullopt;
        case GyroSource::Auto:
        default:
            if (leftConnected)
                return readSensor(JoyDualLeft);
            if (rightConnected)
                return readSensor(JoyDualRight);
            return std::nullopt;
    }
}

void HosPadPath::resetMotion()
{
    for (const HidSixAxisSensorHandle handle : m_sixaxis)
        hidResetSixAxisSensorFusionParameters(handle);
}

void HosPadPath::sendRumble(float left, float right, float freqLow, float freqHigh)
{
    const float amp = std::clamp(std::max(left, right), 0.0f, 1.0f);

    auto* inputMgr = brls::Application::getPlatform()->getInputManager();
    inputMgr->sendRumbleRaw(0, freqLow, freqHigh, amp, amp);
}

} // namespace akira::input
