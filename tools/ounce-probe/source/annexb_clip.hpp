#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class H264Clip
{
public:
    static std::optional<H264Clip> load(const char* path);

    H264Clip(H264Clip&&) = default;
    H264Clip& operator=(H264Clip&&) = default;
    H264Clip(const H264Clip&) = delete;
    H264Clip& operator=(const H264Clip&) = delete;

    std::span<const std::span<const std::uint8_t>> accessUnits() const { return m_units; }
    std::span<const std::uint8_t> codecConfig() const { return m_config; }

private:
    explicit H264Clip(std::vector<std::uint8_t> data);

    std::vector<std::uint8_t> m_data;
    std::vector<std::span<const std::uint8_t>> m_units;
    std::vector<std::uint8_t> m_config;
};
