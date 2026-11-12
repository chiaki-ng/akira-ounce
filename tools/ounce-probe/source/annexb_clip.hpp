#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class AnnexBClip
{
public:
    enum class Codec
    {
        h264,
        hevc,
    };

    static std::optional<AnnexBClip> load(const char* path, Codec codec);

    AnnexBClip(AnnexBClip&&) = default;
    AnnexBClip& operator=(AnnexBClip&&) = default;
    AnnexBClip(const AnnexBClip&) = delete;
    AnnexBClip& operator=(const AnnexBClip&) = delete;

    std::span<const std::span<const std::uint8_t>> accessUnits() const { return m_units; }
    std::span<const std::uint8_t> codecConfig() const { return m_config; }

private:
    AnnexBClip(std::vector<std::uint8_t> data, Codec codec);
    static std::uint8_t nalType(std::uint8_t header, Codec codec);

    std::vector<std::uint8_t> m_data;
    std::vector<std::span<const std::uint8_t>> m_units;
    std::vector<std::uint8_t> m_config;
};
