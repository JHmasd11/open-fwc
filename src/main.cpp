#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/file.hpp>
#include <chrono>
#include "replay.hpp"
#include "checkpoint.hpp"
#include "sweep.hpp"
#include <Geode/loader/Loader.hpp>
#include <Geode/binding/CheckpointObject.hpp>

using namespace geode::prelude;

namespace {
// Only the current PlayLayer owns these observations. Never store player pointers.
struct Recorder {
    GJBaseGameLayer* owner = nullptr;
    CCLabelBMFont* hud = nullptr; // owned by PlayLayer
    fwc::Attempt attempt;
    fwc::Replay replay;
    fwc::ReplayInputGate replayGate;
    fwc::CheckProbe probe;
    fwc::Sweep sweep;
    unsigned sweepSerial = 0;
    bool checkpointEnabled = false;
    std::uintptr_t checkpointToken = 0; // identity only, never dereferenced
    unsigned checkpointSerial = 0, restoreSerial = 0;
    bool localLevel = false, replayEligible = false, insideQueue = false;
    std::string notice;
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
    if (state.replay.phase != fwc::Replay::Phase::Idle) {
        auto const& r = state.replay;
        auto mode = r.running() ? "REPLAY" : r.phase == fwc::Replay::Phase::Finished ? "REPLAY DONE" : "REPLAY STOP";
        text = fmt::format("FWC | {} | {}/{}\nMax error {:.4f} | {}", mode, r.cursor, r.events.size(), r.maxError, r.message);
    } else if (!state.active) text = "FWC | Idle";
    else if (state.pausePending) text = fmt::format("FWC | A{} | {} down / {} up | Paused", state.attemptId, state.attempt.presses, state.attempt.releases);
    else if (state.attempt.events.empty()) text = fmt::format("FWC | A{} | 0 down / 0 up", state.attemptId);
    else {
        auto const& e = state.attempt.events.back();
        text = fmt::format("FWC | A{} | {} down / {} up\nP{} {} | step {} | x {:.1f}",
            state.attemptId, state.attempt.presses, state.attempt.releases,
            e.player, e.down ? "DOWN" : "UP", e.progress, e.x);
    }
    if (state.checkpointEnabled && state.localLevel && state.owner && state.owner->m_isPracticeMode) {
        using P = fwc::CheckProbe::Phase;
        auto const& p = state.probe;
        switch (p.phase) {
            case P::Idle: text += "\nCP | Place manual checkpoint"; break;
            case P::Baseline: text += fmt::format("\nCP | BASELINE {}/{}", p.baseline.size(), fwc::CheckProbe::target); break;
            case P::Ready: text += "\nCP | READY | Die to restore"; break;
            case P::Comparing: text += fmt::format("\nCP | COMPARE {}/{}", p.samples.size(), fwc::CheckProbe::target); break;
            case P::Passed: text += fmt::format("\nCP | MATCH {}/{} | XY {:.6f} VY {:.6f}", p.samples.size(), fwc::CheckProbe::target, p.maxPosition, p.maxVelocity); break;
            case P::Failed: text += fmt::format("\nCP | FAIL | {}", p.reason); break;
        }
    }
    using S = fwc::Sweep::Phase;
    auto const& sw=state.sweep;
    if (sw.busy()) text += fmt::format("\nFW | {}/{} | offset {:+} ticks", sw.trials.size(), sw.offsets.size(), sw.offset());
    else if (sw.phase==S::Done) {
        auto [lo,hi]=sw.window();
        text += fmt::format("\nFW | {}{} ticks | {:+} .. {:+}", (lo==-fwc::Sweep::radius || hi==fwc::Sweep::radius)?">= ":"", hi-lo+1, lo,hi);
    } else if (sw.phase==S::Aborted) text += "\nFW | STOP | " + sw.reason;
    if (!state.notice.empty()) text += "\n" + state.notice;
    state.hud->setString(text.c_str());
}

bool checkpointMode(GJBaseGameLayer* layer) {
    return state.owner == layer && state.checkpointEnabled && state.localLevel &&
        layer->m_isPracticeMode && !layer->m_startPosObject && !state.replay.running();
}
void saveProbe(char const* stage) {
    auto const& p = state.probe;
    auto path = Mod::get()->getSaveDir() / fmt::format("checkpoint-{}-c{}-r{}-{}.csv", state.session, state.checkpointSerial, state.restoreSerial, stage);
    auto result = geode::utils::file::writeString(path, p.csv());
    if (result.isErr()) log::error("[FWC] checkpoint report write failed: {}", result.unwrapErr());
    log::info("[FWC] checkpoint {}: baseline={} compared={} mismatches={} tickOffset={} xy={:.9f} vy={:.9f} reason={} file={}",
        stage, p.baseline.size(), p.samples.size(), p.mismatches, p.restoreTickOffset, p.maxPosition, p.maxVelocity, p.reason, path.string());
    updateHud();
}
void failProbe(char const* reason) {
    if (!state.probe.measuring()) return;
    state.probe.fail(reason); saveProbe("failed");
}
void beginProbe(PlayLayer* layer, CheckpointObject* cp) {
    if (!cp || !checkpointMode(layer) || state.resetGate.depth || state.pausePending || layer->m_isPaused) return;
    auto token = reinterpret_cast<std::uintptr_t>(cp);
    if (token == state.checkpointToken) return; // createCheckpoint + markCheckpoint may return the same object
    if (state.probe.measuring()) failProbe("replaced by new checkpoint");
    state.checkpointToken = token; ++state.checkpointSerial; state.restoreSerial = 0;
    state.probe.begin();
    log::info("[FWC] checkpoint C{} captured at progress={}", state.checkpointSerial, layer->m_gameState.m_currentProgress);
    updateHud();
}
void probeStep(GJBaseGameLayer* layer) {
    if (!checkpointMode(layer) || !state.probe.measuring() || state.resetGate.depth || state.pausePending) return;
    auto p = layer->m_player1;
    if (!p || p->m_isDead || layer->m_gameState.m_isDualMode) { failProbe("dead or unsupported dual state"); return; }
    for (auto const& [button, down] : p->m_holdingButtons) if (down) { failProbe("button held during sample"); return; }
    auto pos = p->getPosition();
    fwc::CheckSample sample;
    sample.progress = layer->m_gameState.m_currentProgress;
    sample.command = layer->m_gameState.m_commandIndex;
    sample.tick = layer->m_tickIndex;
    sample.time = layer->m_gameState.m_levelTime;
    sample.x = pos.x; sample.y = pos.y; sample.vy = p->m_yVelocity; sample.speed = p->m_playerSpeed;
    sample.flags = unsigned(p->m_isOnGround) | (unsigned(p->m_isUpsideDown)<<1) | (unsigned(p->m_isShip)<<2) |
        (unsigned(p->m_isBird)<<3) | (unsigned(p->m_isBall)<<4) | (unsigned(p->m_isDart)<<5) |
        (unsigned(p->m_isRobot)<<6) | (unsigned(p->m_isSpider)<<7) | (unsigned(p->m_isSwing)<<8) | (unsigned(p->m_isGoingLeft)<<9);
    auto& probe = state.probe;
    auto previous = probe.phase;
    probe.sample(sample);
    if (previous != probe.phase) {
        using P = fwc::CheckProbe::Phase;
        saveProbe(probe.phase == P::Ready ? "baseline" : probe.phase == P::Passed ? "matched" : "failed");
    } else if ((probe.phase == fwc::CheckProbe::Phase::Baseline ? probe.baseline.size() : probe.samples.size()) % 15 == 0) updateHud();
}
void saveSweep() {
    auto path=Mod::get()->getSaveDir() / fmt::format("window-{}-s{}.csv",state.session,state.sweepSerial);
    auto result=geode::utils::file::writeString(path,state.sweep.csv());
    if(result.isErr()) log::error("[FWC] window CSV write failed: {}",result.unwrapErr());
    log::info("[FWC] window trials={} reason={} file={}",state.sweep.trials.size(),state.sweep.reason,path.string());
    updateHud();
}
void abortSweep(char const* reason) {
    if(!state.sweep.busy()) return;
    state.sweep.abort(reason); saveSweep();
}
void stopReplay(GJBaseGameLayer* layer, bool finished, char const* reason, bool release) {
    auto& r = state.replay;
    if (!r.running()) return;
    if(state.sweep.running()) abortSweep(reason);
    r.phase = finished ? fwc::Replay::Phase::Finished : fwc::Replay::Phase::Failed;
    r.message = reason;
    auto held = std::move(r.held);
    r.held.clear();
    auto path = Mod::get()->getSaveDir() / fmt::format("replay-{}-a{}-{}.csv", state.session, state.attemptId, finished ? "done" : "stopped");
    auto result = geode::utils::file::writeString(path, r.csv(reason));
    if (result.isErr()) log::error("[FWC] replay report write failed: {}", result.unwrapErr());
    log::info("[FWC] replay {}: {}/{} max error {:.6f} report {}", reason, r.cursor, r.events.size(), r.maxError, path.string());
    updateHud();
    // A reset/quit owns its own input cleanup. For interruption mid-attempt release injected keys.
    if (release) {
        auto session = state.session;
        auto attempt = state.attemptId;
        for (auto const& [p, b] : held) {
            if (state.owner != layer || state.session != session || state.attemptId != attempt) break;
            layer->handleButton(false, b, p == 1);
        }
    }
}
void flushAttempt(char const* reason) {
    if (state.replay.running()) stopReplay(state.owner, false, reason, false);
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
    state.replay = fwc::Replay{};
    state.replayGate = fwc::ReplayInputGate{};
    state.notice.clear();
    state.insideQueue = false;
    state.replayEligible = state.localLevel && state.owner && !state.owner->m_isPracticeMode && !state.owner->m_startPosObject;
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
void armSweep() {
    if (!state.sweep.running() || !state.replayEligible || !state.active) {
        abortSweep("invalid restart state"); return;
    }
    state.active=false;
    state.replay.begin(state.sweep.shifted());
    updateHud();
}
void finishTrial(GJBaseGameLayer* layer, bool alive) {
    if(state.owner!=layer || !state.sweep.running() || !state.replay.running()) return;
    auto session=state.session, attempt=state.attemptId;
    state.sweep.result(alive,state.logicCall,state.replay.cursor,state.replay.maxError);
    stopReplay(layer,alive,alive?"horizon reached":"player died",false);
    saveSweep();
    // Native death retry owns the death animation and scheduled respawn.
    if(!state.sweep.pending() || !alive) return;
    geode::queueInMainThread([session,attempt] {
        if(state.session!=session || state.attemptId!=attempt || !state.owner || !state.sweep.pending()) return;
        auto layer=static_cast<PlayLayer*>(state.owner);
        if(layer->m_isPaused || state.pausePending || state.resetGate.depth) { abortSweep("paused before next trial"); return; }
        layer->resetLevelFromStart();
    });
}
void trace(char const* phase, PlayLayer* layer) {
    if (state.owner != layer) return;
    log::info("[FWC] lifecycle {} A{} active={} paused={} pending={} reset-depth={} held={} progress={}",
        phase, state.attemptId, state.active, layer->m_isPaused, state.pausePending,
        state.resetGate.depth, state.attempt.held.size(), layer->m_gameState.m_currentProgress);
}
fwc::Event observe(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1,
                   fwc::Event::Source source = fwc::Event::Source::Input) {
    auto player = isPlayer1 ? layer->m_player1 : layer->m_player2;
    auto pos = player ? player->getPosition() : CCPoint{0.f, 0.f};
    fwc::Event e;
    e.progress = layer->m_gameState.m_currentProgress;
    e.tickIndex = layer->m_tickIndex;
    e.commandIndex = layer->m_gameState.m_commandIndex;
    e.logicCall = state.logicCall;
    e.insideLogic = state.insideLogic;
    e.insideQueue = state.insideQueue;
    e.halfTick = state.halfTick;
    e.lastTick = state.lastTick;
    e.levelTime = layer->m_gameState.m_levelTime;
    e.totalTime = layer->m_gameState.m_totalTime;
    e.player = isPlayer1 ? 1 : 2;
    e.button = button;
    e.down = down;
    e.x = pos.x; e.y = pos.y;
    e.dead = !player || player->m_isDead;
    e.source = source;
    return e;
}
std::optional<bool> heldButton(GJBaseGameLayer* layer, int playerIndex, int button) {
    auto player = playerIndex == 1 ? layer->m_player1 : layer->m_player2;
    if (!player) return std::nullopt;
    auto it = player->m_holdingButtons.find(button);
    return it != player->m_holdingButtons.end() && it->second;
}
void capture(GJBaseGameLayer* layer, bool down, int button, bool isPlayer1,
             fwc::Event::Source source = fwc::Event::Source::Input) {
    if (!(isPlayer1 ? layer->m_player1 : layer->m_player2)) return;
    auto e = observe(layer, down, button, isPlayer1, source);
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
        if (engaged && state.owner == layer && state.session == session && state.resetGate.end()) {
            startAttempt();
            if(state.sweep.next()) armSweep();
        }
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
        state.checkpointEnabled = Mod::get()->getSettingValue<bool>("checkpoint-probe");
        if (level) { state.levelName = level->m_levelName; state.levelId = level->m_levelID.value(); state.localLevel = level->m_levelType == GJLevelType::Editor; }
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
    CheckpointObject* createCheckpoint() {
        auto cp = PlayLayer::createCheckpoint();
        beginProbe(this, cp);
        return cp;
    }
    CheckpointObject* markCheckpoint() {
        auto cp = PlayLayer::markCheckpoint();
        beginProbe(this, cp);
        return cp;
    }
    void loadFromCheckpoint(CheckpointObject* cp) {
        auto session = state.session;
        bool ours = checkpointMode(this) && cp && reinterpret_cast<std::uintptr_t>(cp) == state.checkpointToken;
        if (checkpointMode(this) && !ours) log::warn("[FWC] native checkpoint load did not match captured checkpoint C{}", state.checkpointSerial);
        if (ours && state.probe.measuring()) failProbe("reset interrupted sampling");
        PlayLayer::loadFromCheckpoint(cp);
        if (ours && state.owner == this && state.session == session && checkpointMode(this)) {
            ++state.restoreSerial;
            if (state.probe.restore()) {
                log::info("[FWC] checkpoint C{} restored R{} progress={}", state.checkpointSerial, state.restoreSerial, m_gameState.m_currentProgress);
                updateHud();
            } else saveProbe("failed");
        }
    }
    void removeCheckpoint(bool first) {
        PlayLayer::removeCheckpoint(first);
        if (state.owner == this && state.checkpointToken && reinterpret_cast<std::uintptr_t>(getLastCheckpoint()) != state.checkpointToken) {
            failProbe("checkpoint removed"); state.checkpointToken = 0; state.probe = fwc::CheckProbe{}; updateHud();
        }
    }
    void resetLevel() {
        ResetScope guard(this, "reset");
        PlayLayer::resetLevel();
    }
    void resetLevelFromStart() {
        if (state.owner == this && state.checkpointToken) { failProbe("restart from start"); state.checkpointToken = 0; state.probe = fwc::CheckProbe{}; }
        ResetScope guard(this, "restart-from-start");
        PlayLayer::resetLevelFromStart();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto session=state.session, attempt=state.attemptId;
        PlayLayer::destroyPlayer(player,object);
        if(state.owner==this && state.session==session && state.attemptId==attempt && state.sweep.running() && m_player1 && m_player1->m_isDead)
            finishTrial(this,false);
    }
    void levelComplete() {
        if (state.owner == this) { abortSweep("level ended before horizon"); failProbe("level complete before sample end"); flushAttempt("complete"); updateHud(); }
        PlayLayer::levelComplete();
    }
    void pauseGame(bool unfocused) {
        auto ours = state.owner == this;
        auto session = state.session;
        if (ours) {
            trace("pause-before", this);
            abortSweep("paused");
            if (state.replay.running()) stopReplay(this, false, "paused", true);
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
        auto session = state.session;
        std::vector<fwc::Event> macro;
        std::string rejection;
        bool scanRequested = state.owner == this && state.active && !checkpointMode(this) &&
            Mod::get()->getSettingValue<bool>("window-scan");
        fwc::Sweep newSweep;
        std::string scanError;
        if(scanRequested) {
            if(!state.replayEligible || m_isPracticeMode || m_startPosObject || m_gameState.m_isDualMode) scanError="Use local single-player level from start";
            else scanError=newSweep.begin(state.attempt,state.logicCall);
        }
        bool requested = !scanRequested && state.owner == this && state.active && !checkpointMode(this) &&
            Mod::get()->getSettingValue<bool>("replay-on-restart");
        if (requested) {
            if (!state.replayEligible || m_isPracticeMode || m_startPosObject) rejection = "Use a local level from start";
            else rejection = fwc::replayRejection(state.attempt);
            if (rejection.empty()) macro = state.attempt.events;
        }
        {
            ResetScope guard(this, "pause-restart");
            PlayLayer::resumeAndRestart(fromStart);
        }
        if (state.owner == this && state.session == session) {
            state.pausePending = m_isPaused;
            if(scanRequested) {
                state.sweep=std::move(newSweep);
                if(scanError.empty()) { ++state.sweepSerial; armSweep(); }
                else { state.notice=scanError; log::warn("[FWC] scan rejected: {}",scanError); }
            } else if (requested && !macro.empty() && state.active && state.replayEligible) {
                state.active = false;
                state.replay.begin(std::move(macro));
                log::info("[FWC] replay armed A{}: {} events", state.attemptId, state.replay.events.size());
            } else if (requested) {
                if (rejection.empty()) rejection = "restart did not reach a valid start";
                state.notice = rejection;
                log::warn("[FWC] replay rejected: {}", rejection);
            }
            trace("resume-restart-after", this);
            updateHud();
        }
    }
    void onQuit() {
        if (state.owner == this) {
            trace("quit", this);
            abortSweep("quit");
            failProbe("quit before sample end");
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
                abortSweep("exit");
                failProbe("exit before sample end");
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
        if (state.owner == this && (state.active || state.replay.running()) && !state.resetGate.depth) {
            if (m_isPracticeMode || m_startPosObject) {
                state.replayEligible = false;
                if (state.replay.running()) stopReplay(this, false, "unsupported mode", true);
            }
            auto progress = m_gameState.m_currentProgress;
            if (state.haveProgress && progress < state.lastProgress) {
                log::info("[FWC] progress rewind {} -> {}", state.lastProgress, progress);
                flushAttempt("progress-rewind");
                startAttempt();
            }
            state.lastProgress = progress;
            state.haveProgress = true;
        }
        bool ours = state.owner == this && (state.active || state.replay.running());
        auto session = state.session;
        auto attemptId = state.attemptId;
        bool oldInside = state.insideLogic, oldHalf = state.halfTick, oldLast = state.lastTick;
        if (ours) {
            ++state.logicCall;
            state.insideLogic = true;
            state.halfTick = isHalfTick;
            state.lastTick = isLastTick;
        }
        if (!isHalfTick) probeStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (ours && state.owner == this && state.session == session && state.attemptId == attemptId) {
            state.insideLogic = oldInside;
            state.halfTick = oldHalf;
            state.lastTick = oldLast;
            if (state.sweep.running()) {
                if (!m_player1 || m_gameState.m_isDualMode || m_isPracticeMode || m_startPosObject) {
                    stopReplay(this,false,"unsupported scan state",true);
                } else if (m_player1->m_isDead) finishTrial(this,false);
                else if (state.logicCall >= state.sweep.horizon) {
                    if (state.replay.next()) stopReplay(this,false,"missing input before horizon",true);
                    else finishTrial(this,true);
                }
            }
            auto next = state.replay.next();
            if (state.replay.running() && next && next->logicCall <= state.logicCall) {
                log::warn("[FWC] missed queue: expected logic={} progress={} tick={} command={} time={:.9f}; after command logic={} progress={} tick={} command={} time={:.9f}",
                    next->logicCall, next->progress, next->tickIndex, next->commandIndex, next->levelTime,
                    state.logicCall, m_gameState.m_currentProgress, m_tickIndex, m_gameState.m_commandIndex, m_gameState.m_levelTime);
                stopReplay(this, false, "missed queue phase", true);
            }
        }
    }
    void processQueuedButtons(float dt, bool clearInputQueue) {
        bool ours = state.owner == this;
        auto session = state.session;
        auto attempt = state.attemptId;
        bool oldQueue = state.insideQueue;
        if (ours) state.insideQueue = true;
        if (ours && state.replay.running() && state.insideLogic && !state.pausePending && !state.resetGate.depth) {
            while (auto next = state.replay.next()) {
                if (next->logicCall > state.logicCall) break;
                auto e = *next;
                auto a = observe(this, e.down, e.button, e.player == 1);
                if (e.logicCall != state.logicCall || !state.replay.matches(a)) break;
                if (a.dead || !std::isfinite(a.x) || !std::isfinite(a.y)) {
                    stopReplay(this, false, "invalid player state", true);
                    break;
                }
                auto before = heldButton(this, e.player, e.button);
                if (!before) {
                    stopReplay(this, false, "missing input target", true);
                    break;
                }
                // A call from processQueuedButtons enters our separate handleButton
                // hook. Permit this one synchronous dispatch instead of swallowing it.
                state.replayGate.injecting = true;
                GJBaseGameLayer::handleButton(e.down, e.button, e.player == 1);
                if (state.owner != this || state.session != session || state.attemptId != attempt) break;
                state.replayGate.injecting = false;
                if (!state.replay.running()) break;
                auto after = heldButton(this, e.player, e.button);
                if (!after) {
                    stopReplay(this, false, "input target lost", true);
                    break;
                }
                bool verified = state.replay.confirm(a, *before, *after);
                log::info("[FWC] dispatch P{} b{} {} tick={} held {} -> {} verified={} cursor={}/{} error={:.6f}",
                    e.player, e.button, e.down ? "DOWN" : "UP", fwc::tickCandidate(a), *before, *after,
                    verified, state.replay.cursor, state.replay.events.size(), state.replay.maxError);
                if (!verified) {
                    stopReplay(this, false, "input not applied", true);
                    break;
                }
                if (!state.replay.next()) {
                    if(state.sweep.running()) { updateHud(); break; }
                    bool matched = state.replay.maxError <= 0.1;
                    stopReplay(this, matched, matched ? "inputs matched" : "position diverged", false);
                    break;
                }
                updateHud();
            }
        }
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        if (ours && state.owner == this && state.session == session && state.attemptId == attempt) state.insideQueue = oldQueue;
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (down && checkpointMode(this) && !state.pausePending && !state.resetGate.depth) failProbe("input during sample");
        // Physical inputs cannot mix with a running diagnostic replay.
        if(state.owner==this && state.sweep.pending() && !state.resetGate.depth) return;
        if (state.owner == this && state.replay.running()) {
            if (state.replayGate.blocks(true)) return;
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        if (state.owner == this && state.active) {
            // Ignore UI presses during pause, but record a real release of an already-held key.
            if (state.pausePending) {
                if (!down) capture(this, down, button, isPlayer1, fwc::Event::Source::PauseHook);
                else if (state.verbose) log::info("[FWC] ignored pause DOWN b{}", button);
            } else capture(this, down, button, isPlayer1);
        }
        // Recording mode is observational; only opt-in replay intercepts inputs.
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
