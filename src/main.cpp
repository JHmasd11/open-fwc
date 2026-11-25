#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include "recorder.hpp"

using namespace geode::prelude;

namespace {
// Only the current PlayLayer owns these observations. Never store player pointers.
struct Recorder {
    GJBaseGameLayer* owner = nullptr;
    CCLabelBMFont* hud = nullptr; // owned by PlayLayer
    fwc::Attempt attempt;
    std::uint64_t session = 0, attemptId = 0, logicCall = 0;
    bool active = false, verbose = true, insideLogic = false, halfTick = false, lastTick = false;
};
Recorder state;
std::uint64_t sessionSerial = 0;

void updateHud() {
    if (!state.hud) return;
    std::string text;
    if (!state.active) text = "FWC | Idle";
    else if (state.attempt.events.empty()) text = fmt::format("FWC | A{} | Ready", state.attemptId);
    else {
        auto const& e = state.attempt.events.back();
        text = fmt::format("FWC | A{} | {} down / {} up\nP{} {} | step {} | x {:.1f}",
            state.attemptId, state.attempt.presses, state.attempt.releases,
            e.player, e.down ? "DOWN" : "UP", e.progress, e.x);
    }
    state.hud->setString(text.c_str());
}

void flushAttempt(char const* reason) {
    state.active = false;
    if (state.attempt.events.empty()) return;
    auto dir = Mod::get()->getSaveDir();
    auto path = dir / fmt::format("fwc-{}-a{}-{}.csv", state.session, state.attemptId, reason);
    auto result = geode::utils::file::writeString(path, state.attempt.csv());
    if (result.isErr()) {
        log::error("[FWC] CSV write failed for attempt {}: {}", state.attemptId, result.unwrapErr());
    } else {
        log::info("[FWC] saved attempt {}: {} events ({})", state.attemptId, state.attempt.events.size(), path.string());
    }
    state.attempt.clear();
}

void startAttempt() {
    state.attempt.clear();
    ++state.attemptId;
    state.logicCall = 0;
    state.insideLogic = state.halfTick = state.lastTick = false;
    state.active = Mod::get()->getSettingValue<bool>("enabled");
    state.verbose = Mod::get()->getSettingValue<bool>("verbose-log");
    if (state.active) log::info("[FWC] attempt {} started", state.attemptId);
    updateHud();
}
}

class $modify(FWCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        // init may call resetLevel internally; only start recording after init completes.
        if (state.owner) flushAttempt("replaced");
        state = Recorder{};
        state.owner = this;
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state.session = static_cast<std::uint64_t>(micros) + (++sessionSerial);
        if (Mod::get()->getSettingValue<bool>("show-hud")) {
            auto label = CCLabelBMFont::create("FWC | Ready", "bigFont.fnt");
            if (label) {
                auto size = CCDirector::sharedDirector()->getWinSize();
                label->setAnchorPoint({0.f, 1.f});
                label->setPosition({5.f, size.height - 22.f});
                label->setScale(0.32f);
                label->setColor({125, 235, 255});
                label->setID("openfwc-recorder-hud");
                this->addChild(label, 999);
                state.hud = label;
            }
        }
        startAttempt();
        return true;
    }
    void resetLevel() {
        bool ours = state.owner == this;
        if (ours) flushAttempt("reset");
        PlayLayer::resetLevel();
        if (ours && state.owner == this) startAttempt();
    }
    void levelComplete() {
        if (state.owner == this) { flushAttempt("complete"); updateHud(); }
        PlayLayer::levelComplete();
    }
    void onExit() {
        if (state.owner == this) {
            flushAttempt("exit");
            state = Recorder{};
        }
        PlayLayer::onExit();
    }
};

class $modify(FWCBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        bool ours = state.owner == this && state.active;
        auto session = state.session;
        auto attemptId = state.attemptId;
        bool oldInside = state.insideLogic, oldHalf = state.halfTick, oldLast = state.lastTick;
        if (ours) {
            ++state.logicCall;
            state.insideLogic = true;
            state.halfTick = isHalfTick;
            state.lastTick = isLastTick;
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (ours && state.owner == this && state.session == session && state.attemptId == attemptId) {
            state.insideLogic = oldInside;
            state.halfTick = oldHalf;
            state.lastTick = oldLast;
        }
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (state.owner == this && state.active) {
            auto player = isPlayer1 ? m_player1 : m_player2;
            if (player) {
                auto pos = player->getPosition();
                fwc::Event e;
                e.progress = m_gameState.m_currentProgress;
                e.tickIndex = m_tickIndex;
                e.commandIndex = m_gameState.m_commandIndex;
                e.logicCall = state.logicCall;
                e.insideLogic = state.insideLogic;
                e.halfTick = state.halfTick;
                e.lastTick = state.lastTick;
                e.levelTime = m_gameState.m_levelTime;
                e.totalTime = m_gameState.m_totalTime;
                e.player = isPlayer1 ? 1 : 2;
                e.button = button;
                e.down = down;
                e.x = pos.x; e.y = pos.y;
                e.dead = player->m_isDead;
                state.attempt.push(e);
                if (state.verbose) log::info("[FWC] A{} #{} P{} {} b{} progress={} tick={} logic={} t={:.6f} xy=({:.2f},{:.2f})",
                    state.attemptId, state.attempt.events.size(), e.player, down ? "DOWN" : "UP", button,
                    e.progress, e.tickIndex, e.logicCall, e.levelTime, e.x, e.y);
                updateHud();
            }
        }
        // Observer only: do not alter, delay, suppress or replay game input.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
