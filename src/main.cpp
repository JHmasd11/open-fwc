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
    fwc::ResetGate resetGate;
    std::string levelName;
    int levelId = 0;
    bool haveProgress = false;
    bool pausePending = false;
    unsigned lastProgress = 0;
    bool active = false, verbose = true, insideLogic = false, halfTick = false, lastTick = false;
};
Recorder state;
std::uint64_t sessionSerial = 0;

void updateHud() {
    if (!state.hud) return;
    std::string text;
    if (!state.active) text = "FWC | Idle";
    else if (state.pausePending) text = fmt::format("FWC | A{} | {} down / {} up | Paused", state.attemptId, state.attempt.presses, state.attempt.releases);
    else if (state.attempt.events.empty()) text = fmt::format("FWC | A{} | 0 down / 0 up", state.attemptId);
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
    auto result = geode::utils::file::writeString(path, state.attempt.csv(state.levelName, state.levelId, reason));
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
    state.haveProgress = false;
    state.pausePending = false;
    state.insideLogic = state.halfTick = state.lastTick = false;
    state.active = Mod::get()->getSettingValue<bool>("enabled");
    state.verbose = Mod::get()->getSettingValue<bool>("verbose-log");
    if (state.active) log::info("[FWC] attempt {} started", state.attemptId);
    updateHud();
}
void trace(char const* phase, PlayLayer* layer) {
    if (state.owner != layer) return;
    log::info("[FWC] lifecycle {} A{} active={} paused={} pending={} reset-depth={} held={} progress={}",
        phase, state.attemptId, state.active, layer->m_isPaused, state.pausePending,
        state.resetGate.depth, state.attempt.held.size(), layer->m_gameState.m_currentProgress);
}
void capture(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1,
             fwc::Event::Source source = fwc::Event::Source::Input) {
    auto player = isPlayer1 ? layer->m_player1 : layer->m_player2;
    if (!player) return;
    auto pos = player->getPosition();
    fwc::Event e;
    e.progress = layer->m_gameState.m_currentProgress;
    e.tickIndex = layer->m_tickIndex;
    e.commandIndex = layer->m_gameState.m_commandIndex;
    e.logicCall = state.logicCall;
    e.insideLogic = state.insideLogic;
    e.halfTick = state.halfTick;
    e.lastTick = state.lastTick;
    e.levelTime = layer->m_gameState.m_levelTime;
    e.totalTime = layer->m_gameState.m_totalTime;
    e.player = isPlayer1 ? 1 : 2;
    e.button = button;
    e.down = down;
    e.x = pos.x; e.y = pos.y;
    e.dead = player->m_isDead;
    e.source = source;
    if (state.attempt.push(e)) {
        if (state.verbose || source != fwc::Event::Source::Input)
            log::info("[FWC] A{} #{} P{} {} b{} progress={} tick={} logic={} t={:.6f} xy=({:.2f},{:.2f}) source={}",
                state.attemptId, state.attempt.events.size(), e.player, down ? "DOWN" : "UP", button,
                e.progress, e.tickIndex, e.logicCall, e.levelTime, e.x, e.y, fwc::sourceName(source));
        updateHud();
    } else if (state.verbose) {
        log::info("[FWC] filtered P{} {} b{} progress={} source={}", e.player, down ? "DOWN" : "UP", button,
            e.progress, fwc::sourceName(source));
    }
}
// A pause may release a held button through PlayerObject rather than handleButton.
// Do not manufacture precise gameplay ticks: mark reconciled state changes as pause-sync.
void reconcileAfterResume(PlayLayer* layer) {
    if (state.owner != layer || !state.active || state.resetGate.depth) return;
    auto held = state.attempt.held; // capture() may erase from the original set.
    for (auto const& [playerIndex, button] : held) {
        auto player = playerIndex == 1 ? layer->m_player1 : layer->m_player2;
        if (!player) continue;
        auto it = player->m_holdingButtons.find(button);
        if (it == player->m_holdingButtons.end() || !it->second) {
            log::info("[FWC] reconcile released P{} b{} after pause", playerIndex, button);
            capture(layer, false, button, playerIndex == 1, fwc::Event::Source::PauseSync);
        }
    }
}
// Guard nested resetLevelFromStart -> resetLevel calls; finalize once.
struct ResetScope {
    GJBaseGameLayer* layer;
    std::uint64_t session;
    bool engaged;
    ResetScope(GJBaseGameLayer* owner, char const* reason)
        : layer(owner), session(state.session), engaged(state.owner == owner) {
        if (engaged && state.resetGate.begin()) {
            flushAttempt(reason);
            updateHud();
        }
    }
    ~ResetScope() {
        if (engaged && state.owner == layer && state.session == session && state.resetGate.end())
            startAttempt();
    }
};
}

class $modify(FWCPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        // init may call resetLevel internally; only start recording after init completes.
        if (state.owner) flushAttempt("replaced");
        state = Recorder{};
        state.owner = this;
        if (level) { state.levelName = level->m_levelName; state.levelId = level->m_levelID.value(); }
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
        ResetScope guard(this, "reset");
        PlayLayer::resetLevel();
    }
    void resetLevelFromStart() {
        ResetScope guard(this, "restart-from-start");
        PlayLayer::resetLevelFromStart();
    }
    void levelComplete() {
        if (state.owner == this) { flushAttempt("complete"); updateHud(); }
        PlayLayer::levelComplete();
    }
    void pauseGame(bool unfocused) {
        auto ours = state.owner == this;
        auto session = state.session;
        if (ours) {
            trace("pause-before", this);
            state.pausePending = true;
        }
        PlayLayer::pauseGame(unfocused);
        if (ours && state.owner == this && state.session == session) {
            state.pausePending = m_isPaused;
            trace("pause-after", this);
            updateHud();
        }
    }
    void resume() {
        auto ours = state.owner == this;
        auto session = state.session;
        if (ours) {
            trace("resume-before", this);
            state.pausePending = true;
        }
        PlayLayer::resume();
        if (ours && state.owner == this && state.session == session) {
            state.pausePending = m_isPaused;
            if (!state.pausePending) reconcileAfterResume(this);
            trace("resume-after", this);
            updateHud();
        }
    }
    void resumeAndRestart(bool fromStart) {
        trace("resume-restart-before", this);
        {
            ResetScope guard(this, "pause-restart");
            PlayLayer::resumeAndRestart(fromStart);
        }
        if (state.owner == this) {
            state.pausePending = m_isPaused;
            trace("resume-restart-after", this);
            updateHud();
        }
    }
    void onQuit() {
        if (state.owner == this) {
            trace("quit", this);
            flushAttempt("exit");
            state = Recorder{};
        }
        PlayLayer::onQuit();
    }
    void onExit() {
        if (state.owner == this) {
            trace("onExit", this);
            // A temporary scene transition during pause is not an actual level quit.
            if (!state.pausePending && !m_isPaused && !state.resetGate.depth) {
                flushAttempt("exit");
                state = Recorder{};
            }
        }
        PlayLayer::onExit();
    }
};

class $modify(FWCBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        // A missed restart can be detected by the engine progress counter rewinding.
        // This fallback is diagnostic; practice/checkpoint restores are not validated.
        if (state.owner == this && state.active && !state.resetGate.depth) {
            auto progress = m_gameState.m_currentProgress;
            if (state.haveProgress && progress < state.lastProgress) {
                log::info("[FWC] progress rewind {} -> {}", state.lastProgress, progress);
                flushAttempt("progress-rewind");
                startAttempt();
            }
            state.lastProgress = progress;
            state.haveProgress = true;
        }
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
            // Ignore UI presses during pause, but record a real release of an already-held key.
            if (state.pausePending) {
                if (!down) capture(this, down, button, isPlayer1, fwc::Event::Source::PauseHook);
                else if (state.verbose) log::info("[FWC] ignored pause DOWN b{}", button);
            } else capture(this, down, button, isPlayer1);
        }
        // Observer only: do not alter, delay, suppress or replay game input.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
