#include "local_session.h"
#include "deck.h"
#include "ai_policy.h"

#include <QTimer>
#include <QDebug>
#include <QRandomGenerator>
#include <random>

namespace {
// AI 每步行动前的延迟（毫秒）。给玩家留出看清对手出牌的时间。
constexpr int kAIMoveDelayMs = 1200;
}

LocalSession::LocalSession(QObject *parent)
    : QObject(parent)
    , m_aiEnabled(true)
{
    // Default: all AI players
    for (int i = 0; i < kPlayerCount; ++i) {
        m_playerTypes[i] = PlayerType::AI;
    }
}

LocalSession::~LocalSession()
{
}

void LocalSession::setPlayerType(int seat, PlayerType type)
{
    if (!isValidSeat(seat))
        return;

    m_playerTypes[seat] = type;
}

PlayerType LocalSession::playerType(int seat) const
{
    if (!isValidSeat(seat))
        return PlayerType::User;
    return m_playerTypes[seat];
}

bool LocalSession::startRound(quint32 seed)
{
    // Generate seed if not provided
    if (seed == 0) {
        std::random_device rd;
        seed = rd();
    }

    const CardList deck = Deck::shuffled(seed);
    const CommandResult result = m_engine.startRound(deck, 0);

    if (result.accepted) {
        processEvents(result.events);
        // Chaos deal-time disappearance: roll per seat after the hands are dealt
        // (and their PrivateHandDealt events already emitted). Vanish events are
        // routed here so the UI re-renders the shrunken hands before play starts.
        if (m_chaosActive && m_chaosDisappearOnDraw) {
            for (int s = 0; s < kPlayerCount; ++s)
                maybeVanishOnDeal(s);
        }
        scheduleAIMove();
        return true;
    }
    return false;
}

CommandResult LocalSession::executeCommand(const GameCommand &command)
{
    // Reject commands for AI-controlled seats
    if (isValidSeat(command.seat) && m_playerTypes[command.seat] == PlayerType::AI) {
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    }

    // Note: chaos out-of-play transform for the PLAYER is applied UI-side in
    // GamePanel::onBtnPlay (which already routes the transformed cards through
    // this engine and shows pre-validation feedback). Transforming here too
    // would double-apply, so the player path executes the command as-is. The AI
    // path (executeAIMove) applies transform itself since it has no UI hook.
    const CommandResult result = m_engine.execute(command);

    if (result.accepted) {
        processEvents(result.events);
        scheduleAIMove();
    }

    return result;
}

GamePhase LocalSession::currentPhase() const
{
    return m_engine.state().phase;
}

int LocalSession::currentSeat() const
{
    return m_engine.state().currentSeat;
}

PlayerType LocalSession::currentPlayerType() const
{
    const int seat = currentSeat();
    if (!isValidSeat(seat))
        return PlayerType::User;
    return m_playerTypes[seat];
}

GameState LocalSession::projectedStateFor(int seat) const
{
    return m_engine.projectedStateFor(seat);
}

const GameState &LocalSession::fullState() const
{
    return m_engine.state();
}

void LocalSession::setAIEnabled(bool enabled)
{
    m_aiEnabled = enabled;
}

void LocalSession::triggerAIMove()
{
    const int seat = currentSeat();
    if (!isValidSeat(seat))
        return;

    if (m_playerTypes[seat] == PlayerType::AI) {
        executeAIMove(seat);
    }
}

void LocalSession::processEvents(const QVector<GameEvent> &events)
{
    for (const GameEvent &event : events) {
        emit eventEmitted(event);

        if (event.type == GameEventType::RoundFinished) {
            emit roundFinished(event.seat);
        }

        // Everyone passed on the bid: the engine voided the round and reset to
        // Waiting. Nobody would otherwise re-deal, so the game hangs. Redeal a
        // fresh round on a new seed (deferred to escape this event iteration).
        if (event.type == GameEventType::RoundVoided) {
            QTimer::singleShot(600, this, [this]() {
                if (currentPhase() == GamePhase::Waiting)
                    startRound();
            });
        }
    }
}

void LocalSession::scheduleAIMove()
{
    if (!m_aiEnabled)
        return;

    const GamePhase phase = currentPhase();
    if (phase != GamePhase::CallingLord && phase != GamePhase::Playing)
        return;

    const int seat = currentSeat();
    if (!isValidSeat(seat))
        return;

    if (m_playerTypes[seat] == PlayerType::AI) {
        // 让 AI 出牌节奏更接近真人：原先 300ms 太快，两个 AI 连续行动只隔 600ms，
        // 玩家来不及看清对手出了什么牌。放慢到 kAIMoveDelayMs 给出反应时间。
        QTimer::singleShot(kAIMoveDelayMs, this, [this, seat]() {
            executeAIMove(seat);
        });
    }
}

void LocalSession::executeAIMove(int seat)
{
    if (!isValidSeat(seat))
        return;

    // Chaos transform applies to AI plays too. Decide the move first, apply the
    // transform, then execute. If the transformed play is illegal the engine
    // rejects it and we fall back to performMove for the standard safety net
    // (re-decide + pass / minimal single) so the game never stalls.
    if (m_chaosActive && m_chaosTransformOnPlay
        && currentPhase() == GamePhase::Playing) {
        GameCommand cmd = AIPolicy::decideCommand(m_engine, seat);
        if (cmd.type == GameCommandType::PlayCards && !cmd.cards.isEmpty()) {
            cmd.cards = maybeTransformPlay(seat, cmd.cards);
            CommandResult tr = m_engine.execute(cmd);
            if (tr.accepted) {
                processEvents(tr.events);
                scheduleAIMove();
                return;
            }
            // Transformed play was illegal — fall through to the full fallback.
        }
    }

    // AI 决策与被拒兜底统一走共享的 AIPolicy（服务器掉线托管复用同一逻辑）。
    // performMove 内部已确认 currentSeat==seat 并处理所有兜底，接受后路由事件并链到下一步。
    CommandResult result = AIPolicy::performMove(m_engine, seat);
    if (result.accepted) {
        processEvents(result.events);
        scheduleAIMove();
    }
}

void LocalSession::setChaosParams(bool active, double disappearChance,
                                  double transformChance, bool disappearOnDraw,
                                  bool transformOnPlay, bool disappearOnIdle)
{
    m_chaosActive = active;
    m_chaosDisappearChance = disappearChance;
    m_chaosTransformChance = transformChance;
    m_chaosDisappearOnDraw = disappearOnDraw;
    m_chaosTransformOnPlay = transformOnPlay;
    m_chaosDisappearOnIdle = disappearOnIdle;
}

bool LocalSession::rollChaos(double chance) const
{
    if (chance <= 0.0) return false;
    if (chance >= 1.0) return true;
    return QRandomGenerator::global()->bounded(10000) < static_cast<int>(chance * 10000);
}

bool LocalSession::vanishRandomCard(int seat)
{
    if (!m_chaosActive || !isValidSeat(seat))
        return false;

    const CardList list = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    if (list.isEmpty())
        return false;

    const Card victim = list[QRandomGenerator::global()->bounded(list.size())];
    Cards toVanish;
    toVanish.add(victim);

    const CommandResult result = m_engine.execute(GameCommand::chaosVanish(seat, toVanish));
    if (result.accepted) {
        processEvents(result.events);
        return true;
    }
    return false;
}

bool LocalSession::tryIdleVanish(int seat)
{
    if (!m_chaosActive || !m_chaosDisappearOnIdle || !isValidSeat(seat))
        return false;
    if (!rollChaos(m_chaosDisappearChance))
        return false;
    return vanishRandomCard(seat);
}

void LocalSession::maybeVanishOnDeal(int seat)
{
    if (!isValidSeat(seat))
        return;

    const CardList list = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    Cards toVanish;
    for (const Card &c : list) {
        // Independent per-card roll (the onDraw disappearance rule).
        if (rollChaos(m_chaosDisappearChance))
            toVanish.add(c);
    }
    if (toVanish.isEmpty())
        return;

    const CommandResult result = m_engine.execute(GameCommand::chaosVanish(seat, toVanish));
    if (result.accepted)
        processEvents(result.events);
}

Cards LocalSession::transformPlayerPlay(const Cards &intended)
{
    // Player shares the same authoritative transform as the AI. The human is the
    // current seat when this runs (called from onBtnPlay on the player's turn);
    // the transformed cards are then played as a normal command (executeCommand
    // does not re-transform, so no double-apply).
    return maybeTransformPlay(currentSeat(), intended);
}

Cards LocalSession::maybeTransformPlay(int seat, const Cards &intended)
{
    if (!m_chaosActive || !m_chaosTransformOnPlay || intended.isEmpty()
        || !isValidSeat(seat))
        return intended;

    if (!rollChaos(m_chaosTransformChance))
        return intended;

    // Pick a random played card to mutate, and a random replacement from the
    // rest of the hand (not already among the played cards).
    CardList outList = intended.toCardList(Cards::NoSort);
    const CardList hand = m_engine.state().players[seat].hand.toCardList(Cards::NoSort);
    if (outList.isEmpty() || hand.isEmpty())
        return intended;

    const int outIdx = QRandomGenerator::global()->bounded(outList.size());
    const Card from = outList[outIdx];

    Card to;
    bool found = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        const Card cand = hand[QRandomGenerator::global()->bounded(hand.size())];
        if (!intended.contains(cand)) {
            to = cand;
            found = true;
            break;
        }
    }
    if (!found)
        return intended;

    outList[outIdx] = to;
    Cards result;
    for (const Card &c : outList)
        result.add(c);

    emit chaosCardsTransformed(seat, from, to);
    return result;
}
