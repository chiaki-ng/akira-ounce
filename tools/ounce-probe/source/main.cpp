#include <switch.h>

#include "annexb_clip.hpp"
#include "decode_run.hpp"
#include "probe_log.hpp"
#include "vulkan_presenter.hpp"

int main(int, char**)
{
    romfsInit();
    const bool sockets = R_SUCCEEDED(socketInitializeDefault());
    if (sockets)
        nxlinkStdio();

    ProbeLog log("sdmc:/switch/ounce-probe.log");
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    log.line("ounce-probe: zero-copy hardware decode and Vulkan present on libounce");
    const auto h264 = AnnexBClip::load("romfs:/clip.h264", AnnexBClip::Codec::h264);
    const auto hevc = AnnexBClip::load("romfs:/clip.hevc", AnnexBClip::Codec::hevc);
    if (!h264 || !hevc)
        log.line("FAIL could not load the romfs clips (h264 {} hevc {})", h264.has_value(), hevc.has_value());

    if (auto presenter = VulkanPresenter::create(log))
    {
        DecodeRun runner(log, *presenter, pad);
        bool stopped = false;
        if (hevc)
        {
            const auto result = runner.run("hevc-codec2", *hevc, static_cast<ViddecCodec>(2), 180);
            stopped = result && result->stopped;
        }
        if (h264 && !stopped)
            runner.run("h264", *h264, ViddecCodec_H264, std::nullopt);
    }
    else
    {
        log.line("FAIL Vulkan setup");
    }

    if (sockets)
        socketExit();
    romfsExit();
    return 0;
}
