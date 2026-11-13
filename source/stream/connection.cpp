#include "stream/connection.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include <borealis.hpp>

#include "cloud/cloud_launch.hpp"
#include "core/chiaki_core.hpp"
#include "core/host.hpp"
#include "core/settings_manager.hpp"
#include "core/wireguard_manager.hpp"
#include "input/sample_cadence.hpp"
#include "stream/input_manager.hpp"
#include "stream/session.hpp"

namespace akira::stream {

namespace play = libchiaking::play;
namespace platform = libchiaking::platform;

Connection::Connection(Session& media, std::weak_ptr<ConnectionListener> listener, bool ps5)
    : media(media)
    , listener(std::move(listener))
    , ps5(ps5)
{
}

Connection::~Connection()
{
    alive.reset();
    worker = platform::Thread{};

    std::shared_ptr<play::Session> ending;
    {
        std::lock_guard lock(sessionMutex);
        ending = std::move(active);
    }
    if (ending)
    {
        if (ending->state() != play::SessionState::leaving)
            static_cast<void>(ending->leave());
        static_cast<void>(ending->wait_until_ended());
        drainJournal(*ending);
    }
    media.detach();
    ending.reset();
}

auto Connection::preferencesFor(Host& host, SettingsManager& settings) -> std::optional<play::Preferences>
{
    const bool vpn = !host.isRemote() && WireGuardManager::instance().isConnected();
    uint32_t height = static_cast<uint32_t>(SettingsManager::resolutionToInt(
        vpn ? settings.getVpnVideoResolution() : settings.getVideoResolution(&host)));
    const auto fps = static_cast<uint32_t>(SettingsManager::fpsToInt(
        vpn ? settings.getVpnVideoFPS() : settings.getVideoFPS(&host)));
    const auto bitrate = static_cast<uint32_t>(std::clamp(
        vpn ? settings.getVpnVideoBitrate() : settings.getVideoBitrate(&host),
        static_cast<int>(play::PictureRequest::minimum_bitrate_kbps),
        static_cast<int>(play::PictureRequest::maximum_bitrate_kbps)));
    const auto codec = libchiaking::media::VideoCodec::h264;
    height = std::min(height, play::PictureRequest::maximum_h264_height);

    const auto picture = play::PictureRequest::create(height, fps, bitrate, codec);
    if (!picture)
    {
        brls::Logger::error("Stream preferences refused: {}p {} fps {} kbps", height, fps, bitrate);
        return std::nullopt;
    }

    auto preferences = tuned(*picture, host.isPS5() ? TakionTarget::PS5 : TakionTarget::PS4, settings);
    if (preferences && vpn)
    {
        if (auto network = host.homeNetwork(); network.network())
            preferences->home = std::move(network);
        else
            brls::Logger::error("Stream over WireGuard has no tunnel network; using the local network");
    }
    return preferences;
}

auto Connection::cloudPreferencesFor(const cloud::CloudLaunch& launch, SettingsManager& settings)
    -> std::optional<play::Preferences>
{
    const bool ps5Cloud = launch.ps5();
    const auto codec = libchiaking::media::VideoCodec::h264;
    auto height = static_cast<uint32_t>(settings.getCloudVideoResolution(ps5Cloud));
    height = std::min(height, play::PictureRequest::maximum_h264_height);
    const auto bitrate = static_cast<uint32_t>(std::clamp(settings.getCloudVideoBitrate(ps5Cloud),
        static_cast<int>(play::PictureRequest::minimum_bitrate_kbps),
        static_cast<int>(play::PictureRequest::maximum_bitrate_kbps)));
    const auto picture = play::PictureRequest::create(height, 60, bitrate, codec);
    if (!picture)
    {
        brls::Logger::error("Cloud stream preferences refused: {}p {} kbps", height, bitrate);
        return std::nullopt;
    }
    return tuned(*picture, ps5Cloud ? TakionTarget::PS5Cloud : TakionTarget::PSNow, settings);
}

auto Connection::tuned(play::PictureRequest picture, TakionTarget target, SettingsManager& settings)
    -> std::optional<play::Preferences>
{
    play::DeveloperTuning::Facts facts;
    if (const int version = settings.getTakionVersion(target); version > 0)
        facts.protocol_version = static_cast<uint32_t>(version);
    facts.packet_loss_max = settings.getPacketLossMax();
    facts.keyframe_on_lost_frame = settings.getRequestIdrOnFecFailure();
    facts.nat_probe_sockets = static_cast<std::size_t>(std::clamp(settings.getPortGuessingSocks(), 0,
        static_cast<int>(play::DeveloperTuning::maximum_nat_probe_sockets)));
    const auto developer = play::DeveloperTuning::create(facts);
    const auto internet = play::InternetOptions::create(settings.getPortGuessing(),
        static_cast<std::size_t>(std::max(settings.getPortGuessingCount(), 0)));
    if (!developer || !internet)
    {
        brls::Logger::error("Stream tuning refused: packet loss {} sockets {}", facts.packet_loss_max, facts.nat_probe_sockets);
        return std::nullopt;
    }

    play::Preferences preferences;
    preferences.picture = picture;
    preferences.sound = play::SoundRequest::create(play::SoundRequest::stereo)
                            .and_then([](const play::SoundRequest& sound) { return sound.capped_at(play::SoundRequest::stereo); })
                            .value_or(play::SoundRequest{});
    preferences.developer = *developer;
    preferences.internet = *internet;
    return preferences;
}

auto Connection::consoleStarter(std::shared_ptr<libchiaking::consoles::Console> console,
                                std::shared_ptr<libchiaking::users::PsUser> player,
                                play::Preferences preferences,
                                libchiaking::consoles::Route route) -> Starter
{
    return [console = std::move(console), player = std::move(player), preferences = std::move(preferences), route](
               libchiaking::media::MediaSink& sink, play::SessionEvents& events, const platform::StopToken& stop) -> Started {
        return console->start(player, preferences, sink, events, route, stop);
    };
}

bool Connection::open(Starter starter)
{
    auto started = platform::Thread::start(ChiakiCore::current().threading(), "stream-input",
        platform::ThreadRole::feedback,
        [this, starter = std::move(starter)](platform::StopToken stop) {
            auto opened = starter(*this, *this, stop);
            if (!opened)
            {
                brls::Logger::error("Stream start refused: kind {}", static_cast<int>(opened.error().kind()));
                post([refusal = opened.error()](Connection&, ConnectionListener& to) { to.onRefused(refusal); });
                return;
            }
            auto running = std::move(opened).value();
            {
                std::lock_guard lock(sessionMutex);
                active = running;
            }
            media.attach(running);
            media.startStreamTimer();
            feed(*running, stop);
        });
    if (!started)
    {
        brls::Logger::error("Stream input thread did not start");
        return false;
    }
    worker = std::move(started).value();
    return true;
}

void Connection::feed(play::Session& running, const platform::StopToken& stop)
{
    auto& core = ChiakiCore::current();
    platform::Sleeper sleeper(core.threading(), core.clock());
    const auto period = std::chrono::duration_cast<std::chrono::microseconds>(akira::input::InputPollPeriod);
    auto next = core.clock().now_microseconds();
    auto journalDue = next;

    controllerInput.set_idle();

    while (!stop.stop_requested() && running.state() != play::SessionState::ended)
    {
        sendInput(running);
        next += static_cast<uint64_t>(period.count());
        const auto now = core.clock().now_microseconds();
        if (now >= journalDue)
        {
            drainJournal(running);
            journalDue = now + static_cast<uint64_t>(journalDrainPeriod.count());
        }
        if (now >= next + static_cast<uint64_t>(period.count()))
            next = now;
        if (next > now && !sleeper.sleep_for(std::chrono::microseconds(next - now), stop))
            break;
    }
    drainJournal(running);
}

void Connection::drainJournal(const play::Session& running)
{
    const auto& journal = running.journal();
    while (const auto entry = journal->read())
        brls::Logger::info("[core] {}", entry->line());
}

void Connection::sendInput(play::Session& running)
{
    if (inputSuppressed.load(std::memory_order_acquire))
    {
        fingerIdTouchId.clear();
        controllerInput.set_idle();
    }
    else
    {
        media.UpdateControllerState(controllerInput, &fingerIdTouchId);
    }

    static_cast<void>(running.controllers().send(libchiaking::controllers::SeatNumber::first(), controllerInput.state()));
}

auto Connection::session() const -> std::shared_ptr<play::Session>
{
    std::lock_guard lock(sessionMutex);
    return active;
}

void Connection::post(std::function<void(Connection&, ConnectionListener&)> work)
{
    brls::sync([token = aliveToken, self = this, to = listener, work = std::move(work)]() {
        if (token.expired())
            return;
        if (auto target = to.lock())
            work(*self, *target);
    });
}

auto Connection::state() const -> std::optional<play::SessionState>
{
    const auto running = session();
    if (!running)
        return std::nullopt;
    return running->state();
}

bool Connection::submitLoginPin(std::string_view pin)
{
    const auto running = session();
    auto code = libchiaking::AccessCode::parse(pin);
    if (!running || !code)
        return false;
    const auto submitted = running->login_pin().submit(std::move(*code));
    if (!submitted)
        brls::Logger::error("Login PIN refused: {}", static_cast<int>(submitted.error()));
    return submitted.has_value();
}

void Connection::leave(bool restConsole)
{
    if (const auto running = session())
        static_cast<void>(running->leave(restConsole ? play::Leave::rest_console : play::Leave::disconnect));
}

void Connection::setInputSuppressed(bool suppressed)
{
    inputSuppressed.store(suppressed, std::memory_order_release);
}

void Connection::on_stream_description(const libchiaking::media::StreamDescription& description)
{
    media.describeStream(description);
}

auto Connection::on_video(const libchiaking::media::VideoUnit& unit) -> libchiaking::media::VideoResult
{
    return media.submitVideo(unit);
}

void Connection::on_video_gap()
{
}

auto Connection::on_audio(const libchiaking::media::AudioUnit& unit) -> libchiaking::media::AudioResult
{
    return media.submitAudio(unit);
}

auto Connection::decoder_name() const -> std::string_view
{
    return "Switch 2 hardware decoder";
}

void Connection::on_feedback(const libchiaking::controllers::ControllerFeedback& feedback)
{
    if (feedback.seat.is_added())
        return;

    if (const auto* haptics = feedback.get<libchiaking::controllers::HapticAudio>())
    {
        if (haptics->playable())
            media.HapticCB(haptics->pcm);
    }
    else if (const auto* rumble = feedback.get<libchiaking::controllers::Rumble>())
    {
        media.SetRumble(rumble->left, rumble->right);
    }
    else if (const auto* haptic = feedback.get<libchiaking::controllers::HapticIntensity>())
    {
        media.SetConsoleVibration(static_cast<uint8_t>(haptic->level));
    }
    else if (feedback.get<libchiaking::controllers::MotionReset>())
    {
        if (auto* input = media.getInputManager())
            input->resetMotionControls();
    }
}

void Connection::on_state(play::SessionState state)
{
    if (state == play::SessionState::playing)
        post([](Connection&, ConnectionListener& to) { to.onPlaying(); });
}

void Connection::on_ended(const play::Outcome& outcome)
{
    post([outcome](Connection&, ConnectionListener& to) { to.onEnded(outcome); });
}

void Connection::on_connect_progress(const play::ConnectProgress& progress)
{
    post([progress](Connection&, ConnectionListener& to) { to.onConnectProgress(progress); });
}

void Connection::on_launch_progress(const play::LaunchProgress& progress)
{
    post([progress](Connection&, ConnectionListener& to) { to.onLaunchProgress(progress); });
}

void Connection::on_login_pin_needed(bool previousWasWrong)
{
    post([previousWasWrong](Connection&, ConnectionListener& to) { to.onLoginPinNeeded(previousWasWrong); });
}

}
