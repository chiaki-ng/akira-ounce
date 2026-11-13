#include "decode_run.hpp"

#include <chrono>

#include "stream/hardware_decoder.hpp"
#include "util/crash_report.hpp"

DecodeRun::DecodeRun(ProbeLog& log, VulkanPresenter& presenter, PadState& pad) : m_log(log), m_presenter(presenter), m_pad(pad)
{
}

std::optional<DecodeRun::Result> DecodeRun::run(const char* name, const AnnexBClip& clip, ViddecCodec codec,
    std::optional<std::uint64_t> unitLimit)
{
    m_log.line("{}: codec {} {} access units, codec config {} bytes", name, static_cast<int>(codec), clip.accessUnits().size(),
        clip.codecConfig().size());
    auto created = akira::stream::HardwareDecoder::create({codec, 1920, 1088, clip.codecConfig()});
    if (!created)
    {
        m_log.line("{}: videoDecoderCreate failed 0x{:x}", name, created.error());
        return std::nullopt;
    }
    auto decoder = std::move(created).value();

    Result result;
    double decodeMsTotal = 0.0;
    auto windowStart = std::chrono::steady_clock::now();
    std::uint64_t windowPresented = 0;
    const auto units = clip.accessUnits();

    while (appletMainLoop() && (!unitLimit || result.submitted < *unitLimit))
    {
        padUpdate(&m_pad);
        if (padGetButtonsDown(&m_pad) & HidNpadButton_X)
        {
            m_log.line("deliberate crash requested");
            akira::CrashReport::stage("deliberate crash");
            *static_cast<volatile int*>(nullptr) = 1;
        }
        if (padGetButtonsDown(&m_pad) & HidNpadButton_Plus)
        {
            result.stopped = true;
            break;
        }

        const auto start = std::chrono::steady_clock::now();
        const auto timestamp = static_cast<std::int64_t>(result.submitted) * 16'666'667;
        if (auto submitted = decoder->submit(units[result.submitted % units.size()], timestamp, std::chrono::milliseconds(100)); !submitted)
        {
            m_log.line("{}: videoDecoderSubmit failed 0x{:x} at unit {}", name, submitted.error(), result.submitted);
            if (++result.failures > 10)
                break;
            continue;
        }
        result.submitted++;

        auto frame = decoder->nextFrame(std::chrono::milliseconds(50));
        if (!frame)
            continue;
        if (result.decoded++ == 0)
        {
            m_log.line("{}: first frame {}x{} slot {} after {} submits", name, (*frame)->width(), (*frame)->height(), (*frame)->slot(),
                result.submitted);
            akira::CrashReport::stage("import orders");
            m_presenter.probeImportOrders();
            akira::CrashReport::stage("h264 loop");
        }
        decodeMsTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        if (m_presenter.present(*frame))
        {
            result.presented++;
            windowPresented++;
        }
        else if (++result.failures > 10)
        {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - windowStart >= std::chrono::seconds(2))
        {
            const double seconds = std::chrono::duration<double>(now - windowStart).count();
            m_log.line("{}: decoded {} presented {} | {:.1f} fps | avg submit+decode {:.2f} ms", name, result.decoded, result.presented,
                windowPresented / seconds, decodeMsTotal / result.decoded);
            windowStart = now;
            windowPresented = 0;
        }
    }

    m_log.line("RESULT {} {}: submitted {} decoded {} presented {} failures {}", name,
        result.presented > 0 && result.failures == 0 ? "PASS" : "FAIL", result.submitted, result.decoded, result.presented,
        result.failures);
    return result;
}
