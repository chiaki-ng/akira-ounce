#pragma once

#include <switch.h>

#include <cstdint>
#include <optional>

#include "annexb_clip.hpp"
#include "probe_log.hpp"
#include "vulkan_presenter.hpp"

class DecodeRun
{
public:
    struct Result
    {
        std::uint64_t submitted = 0;
        std::uint64_t decoded = 0;
        std::uint64_t presented = 0;
        std::uint64_t failures = 0;
        bool stopped = false;
    };

    DecodeRun(ProbeLog& log, VulkanPresenter& presenter, PadState& pad);

    std::optional<Result> run(const char* name, const AnnexBClip& clip, ViddecCodec codec, std::optional<std::uint64_t> unitLimit);

private:
    ProbeLog& m_log;
    VulkanPresenter& m_presenter;
    PadState& m_pad;
};
