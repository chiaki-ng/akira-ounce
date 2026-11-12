#include "annexb_clip.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

std::optional<AnnexBClip> AnnexBClip::load(const char* path, Codec codec)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (data.empty())
        return std::nullopt;
    AnnexBClip clip(std::move(data), codec);
    if (clip.m_units.empty() || clip.m_config.empty())
        return std::nullopt;
    return clip;
}

std::uint8_t AnnexBClip::nalType(std::uint8_t header, Codec codec)
{
    return codec == Codec::h264 ? header & 0x1F : (header >> 1) & 0x3F;
}

AnnexBClip::AnnexBClip(std::vector<std::uint8_t> data, Codec codec) : m_data(std::move(data))
{
    constexpr std::array<std::uint8_t, 4> startCode{0x00, 0x00, 0x00, 0x01};
    const std::uint8_t accessUnitDelimiter = codec == Codec::h264 ? 9 : 35;
    const std::array<std::uint8_t, 3> parameterSets = codec == Codec::h264 ? std::array<std::uint8_t, 3>{7, 8, 0xFF}
                                                                           : std::array<std::uint8_t, 3>{32, 33, 34};

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
    std::array<bool, 3> seen{false, false, false};
    for (std::size_t n = 0; n + 1 < starts.size(); n++)
    {
        const std::size_t begin = starts[n];
        const std::size_t header = begin + (bytes[begin + 2] == 1 ? 3 : 4);
        const std::size_t end = starts[n + 1];
        const std::uint8_t type = nalType(bytes[header], codec);
        if (type == accessUnitDelimiter)
        {
            if (unitStart)
                m_units.push_back(bytes.subspan(*unitStart, begin - *unitStart));
            unitStart = begin;
        }
        const auto set = std::ranges::find(parameterSets, type);
        if (set != parameterSets.end() && !seen[set - parameterSets.begin()])
        {
            seen[set - parameterSets.begin()] = true;
            std::ranges::copy(startCode, std::back_inserter(m_config));
            std::ranges::copy(bytes.subspan(header, end - header), std::back_inserter(m_config));
        }
    }
    if (unitStart)
        m_units.push_back(bytes.subspan(*unitStart));
}
