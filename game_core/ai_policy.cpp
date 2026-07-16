#include "ai_policy.h"

#include "game_command.h"
#include "playhand.h"
#include "strategy.h"
#include "player.h"

#include <QDebug>

namespace AIPolicy {

namespace {

// 叫分阶段：按手牌权重定"期望叫分"，仅当严格高于当前 highestBid 才叫，否则不叫(0)。
// （逻辑与 LocalSession 历史实现一致；集中到此处共享。）
GameCommand decideBid(const GameState &state, int seat)
{
    const Cards &hand = state.players[seat].hand;
    int weight = 0;
    weight += hand.pointCount(Card::Card_BJ) * 6;  // 大王
    weight += hand.pointCount(Card::Card_SJ) * 5;  // 小王
    weight += hand.pointCount(Card::Card_2) * 4;   // 2
    weight += hand.pointCount(Card::Card_A) * 3;   // A
    weight += hand.pointCount(Card::Card_K) * 2;   // K

    int desired = 0;
    if (weight >= 18)
        desired = 3;
    else if (weight >= 14)
        desired = 2;
    else if (weight >= 8)
        desired = 1;

    const int bid = (desired > state.highestBid) ? desired : 0;
    return GameCommand::callLord(seat, bid);
}

// 出牌阶段：用 Strategy 决策。Strategy 依赖旧的三人 Player 链表（读取
// getNextPlayer/getPendPlayer 的角色与剩余牌数），单个孤立 Player 会空指针崩溃，
// 故在栈上重建完整三人环，让所有指针在整个策略调用期间有效。
Cards decidePlay(const GameState &state, int seat)
{
    const Cards &hand = state.players[seat].hand;
    const Cards &pending = state.pendingCards;
    const int pendingSeat = state.pendingSeat;
    const bool isLeader = (pendingSeat == seat || pending.isEmpty());

    Player seats[kPlayerCount];
    for (int s = 0; s < kPlayerCount; ++s) {
        seats[s].storeDispatchCard(state.players[s].hand);
        seats[s].setRole(state.players[s].role == PlayerRole::Lord
                             ? Player::Lord
                             : Player::Farmer);
    }
    for (int s = 0; s < kPlayerCount; ++s) {
        seats[s].setNextPlayer(&seats[nextSeat(s)]);
        seats[s].setPrevPlayer(&seats[previousSeat(s)]);
    }

    Player &aiPlayer = seats[seat];
    if (!isLeader && isValidSeat(pendingSeat))
        aiPlayer.storePendingInfo(&seats[pendingSeat], pending);
    else
        aiPlayer.storePendingInfo(nullptr, Cards());

    Strategy strategy(&aiPlayer, hand);
    Cards aiPlay = strategy.makeStrategy();

    if (aiPlay.isEmpty() && isLeader) {
        // 领出者不能过（引擎会拒 CannotPassWhenLeading 导致死锁），兜底出最小单张。
        const CardList sorted = hand.toCardList(Cards::Asc);
        if (!sorted.isEmpty()) {
            Cards single;
            single.add(sorted.first());
            aiPlay = single;
        }
    }
    return aiPlay;
}

} // namespace

GameCommand decideCommand(const GameEngine &engine, int seat)
{
    if (!isValidSeat(seat))
        return GameCommand::pass(kInvalidSeat);

    const GameState &state = engine.state();
    if (state.currentSeat != seat)
        return GameCommand::pass(kInvalidSeat);

    if (state.phase == GamePhase::CallingLord)
        return decideBid(state, seat);
    if (state.phase == GamePhase::Playing) {
        const Cards aiPlay = decidePlay(state, seat);
        return aiPlay.isEmpty() ? GameCommand::pass(seat)
                                : GameCommand::playCards(seat, aiPlay);
    }
    return GameCommand::pass(kInvalidSeat);
}

CommandResult performMove(GameEngine &engine, int seat)
{
    if (!isValidSeat(seat))
        return CommandResult::rejected(GameError::InvalidSeat);

    const GameState &state = engine.state();
    if (state.currentSeat != seat)
        return CommandResult::rejected(GameError::InvalidSeat);

    const GamePhase phase = state.phase;
    CommandResult result;

    if (phase == GamePhase::CallingLord) {
        result = engine.execute(decideBid(state, seat));
    } else if (phase == GamePhase::Playing) {
        const Cards aiPlay = decidePlay(state, seat);
        result = aiPlay.isEmpty()
                     ? engine.execute(GameCommand::pass(seat))
                     : engine.execute(GameCommand::playCards(seat, aiPlay));
    } else {
        return CommandResult::rejected(GameError::InvalidPhase);
    }

    if (result.accepted)
        return result;

    // 被拒兜底：保持牌局流动，绝不卡死。
    qWarning() << "AIPolicy: move rejected for seat" << seat
               << "error" << static_cast<int>(result.error) << "- applying fallback";

    if (phase == GamePhase::CallingLord) {
        return engine.execute(GameCommand::callLord(seat, 0)); // 强制不叫
    }

    // 出牌阶段：先试过牌
    CommandResult passResult = engine.execute(GameCommand::pass(seat));
    if (passResult.accepted)
        return passResult;

    // 过牌也被拒（领出者不能过）：出最小单张避免永久停滞。
    const CardList sorted = engine.state().players[seat].hand.toCardList(Cards::Asc);
    if (!sorted.isEmpty()) {
        Cards single;
        single.add(sorted.first());
        return engine.execute(GameCommand::playCards(seat, single));
    }
    return passResult; // 无牌可出（异常），返回被拒结果
}

} // namespace AIPolicy
