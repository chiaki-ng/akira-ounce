#include "h264_clip.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

std::optional<H264Clip> H264Clip::load(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (data.empty())
        return std::nullopt;
    H264Clip clip(std::move(data));
    if (clip.m_units.empty() || clip.m_config.empty())
        return std::nullopt;
    return clip;
}

H264Clip::H264Clip(std::vector<std::uint8_t> data) : m_data(std::move(data))
{
    constexpr std::uint8_t accessUnitDelimiter = 9;
    constexpr std::uint8_t sequenceParameterSet = 7;
    constexpr std::uint8_t pictureParameterSet = 8;
    constexpr std::array<std::uint8_t, 4> startCode{0x00, 0x00, 0x00, 0x01};

    const std::span<const std::uint8_t> bytes(m_data);
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i + 3 <= bytes.size(); i++)
    {
        if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
        {
            starts.push_back(i > 0 && bytes[i - 1] == 0 ? i - 1 : i);
            i += 2;
        }
    }
    starts.push_back(bytes.size());

    std::optional<std::size_t> unitStart;
    bool haveSps = false;
    bool havePps = false;
    for (std::size_t n = 0; n + 1 < starts.size(); n++)
    {
        const std::size_t begin = starts[n];
        const std::size_t header = begin + (bytes[begin + 2] == 1 ? 3 : 4);
        const std::size_t end = starts[n + 1];
        const std::uint8_t type = bytes[header] & 0x1F;
        if (type == accessUnitDelimiter)
        {
            if (unitStart)
                m_units.push_back(bytes.subspan(*unitStart, begin - *unitStart));
            unitStart = begin;
        }
        const bool parameterSet = (type == sequenceParameterSet && !haveSps) || (type == pictureParameterSet && !havePps);
        if (parameterSet)
        {
            std::ranges::copy(startCode, std::back_inserter(m_config));
            std::ranges::copy(bytes.subspan(header, end - header), std::back_inserter(m_config));
            haveSps = haveSps || type == sequenceParameterSet;
            havePps = havePps || type == pictureParameterSet;
        }
    }
    if (unitStart)
        m_units.push_back(bytes.subspan(*unitStart));
}
