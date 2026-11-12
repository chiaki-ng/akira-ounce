#include <switch.h>

#include <chrono>

#include "h264_clip.hpp"
#include "probe_log.hpp"
#include "stream/hardware_decoder.hpp"
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
    const auto clip = H264Clip::load("romfs:/clip.h264");
    if (!clip)
        log.line("FAIL could not load romfs:/clip.h264");

    auto presenter = clip ? VulkanPresenter::create(log) : nullptr;
    if (clip && !presenter)
        log.line("FAIL Vulkan setup");

    std::shared_ptr<akira::stream::HardwareDecoder> decoder;
    if (presenter)
    {
        log.line("clip: {} access units, codec config {} bytes", clip->accessUnits().size(), clip->codecConfig().size());
        auto created = akira::stream::HardwareDecoder::create({1920, 1088, clip->codecConfig()});
        if (created)
            decoder = std::move(created).value();
        else
            log.line("FAIL videoDecoderCreate 0x{:x}", created.error());
    }

    std::uint64_t submitted = 0;
    std::uint64_t decoded = 0;
    std::uint64_t presented = 0;
    std::uint64_t failures = 0;
    double decodeMsTotal = 0.0;
    auto windowStart = std::chrono::steady_clock::now();
    std::uint64_t windowPresented = 0;
    bool firstFrameLogged = false;

    while (decoder && appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;

        const auto units = clip->accessUnits();
        const auto unit = units[submitted % units.size()];
        const auto start = std::chrono::steady_clock::now();
        const auto timestamp = static_cast<std::int64_t>(submitted) * 16'666'667;
        if (auto result = decoder->submit(unit, timestamp, std::chrono::milliseconds(100)); !result)
        {
            log.line("FAIL videoDecoderSubmit 0x{:x} at unit {}", result.error(), submitted);
            if (++failures > 10)
                break;
            continue;
        }
        submitted++;

        auto frame = decoder->nextFrame(std::chrono::milliseconds(50));
        if (!frame)
            continue;
        decoded++;
        decodeMsTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!firstFrameLogged)
        {
            log.line("first frame {}x{} slot {} after {} submits", (*frame)->width(), (*frame)->height(), (*frame)->slot(), submitted);
            firstFrameLogged = true;
        }
        if (presenter->present(*frame))
        {
            presented++;
            windowPresented++;
        }
        else if (++failures > 10)
        {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - windowStart >= std::chrono::seconds(2))
        {
            const double seconds = std::chrono::duration<double>(now - windowStart).count();
            log.line("decoded {} presented {} | {:.1f} fps | avg submit+decode {:.2f} ms", decoded, presented,
                windowPresented / seconds, decoded ? decodeMsTotal / decoded : 0.0);
            windowStart = now;
            windowPresented = 0;
        }
    }

    log.line("RESULT {}: submitted {} decoded {} presented {} failures {}", presented > 0 && failures == 0 ? "PASS" : "FAIL", submitted,
        decoded, presented, failures);
    presenter.reset();
    decoder.reset();
    if (sockets)
        socketExit();
    romfsExit();
    return 0;
}
