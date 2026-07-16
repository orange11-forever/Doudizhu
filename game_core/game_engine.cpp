#include "game_engine.h"
#include "deck.h"
#include "playhand.h"

const GameState &GameEngine::state() const
{
    return m_state;
}

CommandResult GameEngine::startRound(const CardList &deck, int firstSeat)
{
    if (m_state.phase != GamePhase::Waiting
        && m_state.phase != GamePhase::RoundFinished)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(firstSeat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (!Deck::isValid(deck))
        return CommandResult::rejected(GameError::InvalidDeck);

    const std::array<int, kPlayerCount> scores = {
        m_state.players[0].score,
        m_state.players[1].score,
        m_state.players[2].score,
    };

    GameState next;
    next.roundNumber = m_state.roundNumber + 1;
    for (int seat = 0; seat < kPlayerCount; ++seat)
        next.players[seat].score = scores[seat];

    for (int index = 0; index < 51; ++index)
        next.players[index % kPlayerCount].hand.add(deck[index]);
    for (int index = 51; index < 54; ++index)
        next.bottomCards.add(deck[index]);

    // Each player is dealt 17; the lord later gains the 3 bottom cards.
    for (int seat = 0; seat < kPlayerCount; ++seat) {
        next.players[seat].handCount = next.players[seat].hand.cardCount();
        next.players[seat].playsMade = 0;
    }

    next.phase = GamePhase::CallingLord;
    next.currentSeat = firstSeat;
    m_state = next;

    CommandResult result;
    result.accepted = true;
    result.events.append({GameEventType::RoundStarted, firstSeat,
                          next.roundNumber, {}});
    for (int seat = 0; seat < kPlayerCount; ++seat)
        result.events.append({GameEventType::PrivateHandDealt, seat, 17,
                              next.players[seat].hand});
    result.events.append({GameEventType::TurnChanged, firstSeat, 0, {}});

    Q_ASSERT(validateState() == GameError::None);
    return result;
}

CommandResult GameEngine::execute(const GameCommand &command)
{
    if (m_state.phase == GamePhase::RoundFinished)
        return CommandResult::rejected(GameError::RoundAlreadyFinished);

    CommandResult result;
    switch (command.type) {
    case GameCommandType::CallLord:
        result = executeCallLord(command);
        break;
    case GameCommandType::PlayCards:
        result = executePlayCards(command);
        break;
    case GameCommandType::Pass:
        result = executePass(command);
        break;
    case GameCommandType::ChaosVanish:
        result = executeChaosVanish(command);
        break;
    default:
        return CommandResult::rejected(GameError::InvalidPhase);
    }

    if (result.accepted) {
        Q_ASSERT(validateState() == GameError::None);
    }
    return result;
}

CommandResult GameEngine::executeCallLord(const GameCommand &command)
{
    if (m_state.phase != GamePhase::CallingLord)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (command.bid < 0 || command.bid > 3)
        return CommandResult::rejected(GameError::InvalidBid);
    if (command.bid > 0 && command.bid <= m_state.highestBid)
        return CommandResult::rejected(GameError::BidNotHighEnough);

    CommandResult result;
    result.accepted = true;

    if (command.bid > m_state.highestBid) {
        m_state.highestBid = command.bid;
        m_state.highestBidder = command.seat;
    }
    m_state.bidCount++;

    result.events.append({GameEventType::BidAccepted, command.seat, command.bid, {}});

    // Bid 3 selects lord immediately
    if (command.bid == 3) {
        selectLord(command.seat, command.bid, &result.events);
        return result;
    }

    // After three bids, resolve
    if (m_state.bidCount == 3) {
        if (m_state.highestBid == 0) {
            // All passed - void the round
            m_state.phase = GamePhase::Waiting;
            m_state.currentSeat = kInvalidSeat;
            m_state.highestBid = 0;
            m_state.highestBidder = kInvalidSeat;
            m_state.bidCount = 0;
            m_state.roundNumber--;  // Reset round number since round was voided
            result.events.append({GameEventType::RoundVoided, kInvalidSeat, 0, {}});
        } else {
            // Select highest bidder as lord
            selectLord(m_state.highestBidder, m_state.highestBid, &result.events);
        }
        return result;
    }

    // Advance to next seat
    m_state.currentSeat = nextSeat(m_state.currentSeat);
    result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, {}});
    return result;
}

CommandResult GameEngine::executePlayCards(const GameCommand &command)
{
    if (m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (!m_state.players[command.seat].hand.contains(command.cards))
        return CommandResult::rejected(GameError::CardsNotOwned);

    const PlayHand candidate(command.cards);
    if (candidate.getHandType() == PlayHand::Hand_Unknown
        || candidate.getHandType() == PlayHand::Hand_Pass)
        return CommandResult::rejected(GameError::InvalidHand);

    if (!m_state.pendingCards.isEmpty()) {
        const PlayHand pending(m_state.pendingCards);
        if (!candidate.canBeat(pending))
            return CommandResult::rejected(GameError::HandDoesNotBeatPending);
    }

    CommandResult result;
    result.accepted = true;

    // Remove cards from player's hand and add to played cards
    m_state.players[command.seat].hand.remove(command.cards);
    m_state.players[command.seat].handCount = m_state.players[command.seat].hand.cardCount();
    m_state.players[command.seat].playsMade++;
    m_state.playedCards.add(command.cards);
    m_state.pendingCards = command.cards;
    m_state.pendingSeat = command.seat;
    m_state.passCount = 0;

    // Handle bomb multiplier
    if (candidate.getHandType() == PlayHand::Hand_Bomb
        || candidate.getHandType() == PlayHand::Hand_Bomb_Jokers) {
        m_state.multiplier *= 2;
        result.events.append({GameEventType::MultiplierChanged, command.seat,
                              m_state.multiplier, {}});
    }

    result.events.append({GameEventType::CardsPlayed, command.seat, 0, command.cards});

    // Check for win condition
    if (m_state.players[command.seat].hand.isEmpty()) {
        finishRound(command.seat, &result.events);
        return result;
    }

    // Advance to next seat
    m_state.currentSeat = nextSeat(m_state.currentSeat);
    result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, {}});
    return result;
}

CommandResult GameEngine::executePass(const GameCommand &command)
{
    if (m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.seat != m_state.currentSeat)
        return CommandResult::rejected(GameError::NotCurrentPlayer);
    if (m_state.pendingCards.isEmpty() || m_state.pendingSeat == command.seat)
        return CommandResult::rejected(GameError::CannotPassWhenLeading);

    CommandResult result;
    result.accepted = true;

    m_state.passCount++;
    result.events.append({GameEventType::PlayerPassed, command.seat, 0, {}});

    // Two passes return lead to pending seat
    if (m_state.passCount == 2) {
        m_state.currentSeat = m_state.pendingSeat;
        m_state.pendingCards.clear();
        m_state.pendingSeat = kInvalidSeat;
        m_state.passCount = 0;
        // Tell view-only clients the trick ended and pending was cleared;
        // TurnChanged alone cannot convey this.
        result.events.append({GameEventType::TrickReset, m_state.currentSeat, 0, {}});
    } else {
        m_state.currentSeat = nextSeat(m_state.currentSeat);
    }

    result.events.append({GameEventType::TurnChanged, m_state.currentSeat, 0, {}});
    return result;
}

CommandResult GameEngine::executeChaosVanish(const GameCommand &command)
{
    // Chaos mode: remove the given cards from the seat's hand, out of turn.
    // Valid in CallingLord (deal-time disappearance) and Playing (idle timeout).
    if (m_state.phase != GamePhase::CallingLord && m_state.phase != GamePhase::Playing)
        return CommandResult::rejected(GameError::InvalidPhase);
    if (!isValidSeat(command.seat))
        return CommandResult::rejected(GameError::InvalidSeat);
    if (command.cards.isEmpty())
        return CommandResult::rejected(GameError::InvalidHand);
    // The cards must actually be in the seat's hand (the session picks from the
    // authoritative hand, but guard against stale picks / duplicates).
    if (!m_state.players[command.seat].hand.contains(command.cards))
        return CommandResult::rejected(GameError::CardsNotOwned);

    m_state.players[command.seat].hand.remove(command.cards);
    m_state.players[command.seat].handCount = m_state.players[command.seat].hand.cardCount();
    // Move the vanished cards to a side pile so the 54-card / no-duplicate
    // invariant in validateState() stays honest (the cards still exist, just
    // out of play). Without this the assert in execute() would fire.
    m_state.vanishedCards.add(command.cards);

    CommandResult result;
    result.accepted = true;
    result.events.append({GameEventType::CardVanished, command.seat, 0, command.cards});
    return result;
}

void GameEngine::selectLord(int seat, int bid, QVector<GameEvent> *events)
{
    m_state.players[seat].role = PlayerRole::Lord;
    for (int i = 0; i < kPlayerCount; ++i) {
        if (i != seat)
            m_state.players[i].role = PlayerRole::Farmer;
    }

    // Move bottom cards to lord's hand
    m_state.revealedBottomCards = m_state.bottomCards;
    m_state.players[seat].hand.add(m_state.bottomCards);
    m_state.bottomCards.clear();
    m_state.players[seat].handCount = m_state.players[seat].hand.cardCount();  // now 20

    m_state.phase = GamePhase::Playing;
    m_state.multiplier = bid;
    m_state.currentSeat = seat;

    events->append({GameEventType::LordSelected, seat, bid, m_state.revealedBottomCards});
    events->append({GameEventType::TurnChanged, seat, 0, {}});
}

void GameEngine::finishRound(int winnerSeat, QVector<GameEvent> *events)
{
    // Find lord seat
    int lordSeat = kInvalidSeat;
    for (int i = 0; i < kPlayerCount; ++i) {
        if (m_state.players[i].role == PlayerRole::Lord) {
            lordSeat = i;
            break;
        }
    }

    // Defensive check: lord must exist in Playing phase
    Q_ASSERT(isValidSeat(lordSeat));
    if (!isValidSeat(lordSeat))
        return;

    // Determine if lord won
    bool lordWins = (winnerSeat == lordSeat);

    // 春天 / 反春天 doubling:
    //  - 春天 (spring): lord wins while neither farmer ever played a card.
    //  - 反春天 (anti-spring): a farmer wins while the lord played only its
    //    opening lead (exactly one play).
    // Either doubles the final multiplier.
    bool spring = false;
    if (lordWins) {
        spring = true;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat && m_state.players[i].playsMade > 0) {
                spring = false;
                break;
            }
        }
    } else {
        spring = (m_state.players[lordSeat].playsMade <= 1);
    }
    if (spring) {
        m_state.multiplier *= 2;
        events->append({GameEventType::MultiplierChanged, lordSeat,
                        m_state.multiplier, {}});
    }

    const int m = m_state.multiplier;

    // Apply scoring
    if (lordWins) {
        // Lord wins: lord +2m, each farmer -m
        m_state.players[lordSeat].score += 2 * m;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat)
                m_state.players[i].score -= m;
        }
    } else {
        // Farmer wins: lord -2m, each farmer +m
        m_state.players[lordSeat].score -= 2 * m;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (i != lordSeat)
                m_state.players[i].score += m;
        }
    }

    m_state.winnerSeat = winnerSeat;
    m_state.phase = GamePhase::RoundFinished;
    m_state.currentSeat = kInvalidSeat;

    // Emit score changed events
    for (int i = 0; i < kPlayerCount; ++i) {
        events->append({GameEventType::ScoreChanged, i, m_state.players[i].score, {}});
    }
    events->append({GameEventType::RoundFinished, winnerSeat, 0, {}});
}

GameError GameEngine::validateState() const
{
    // Collect all cards and count
    Cards allCards;
    int inputCount = 0;

    for (int i = 0; i < kPlayerCount; ++i) {
        const CardList hand = m_state.players[i].hand.toCardList(Cards::NoSort);
        inputCount += hand.size();
        for (const Card &card : hand)
            allCards.add(card);
    }

    const CardList bottom = m_state.bottomCards.toCardList(Cards::NoSort);
    inputCount += bottom.size();
    for (const Card &card : bottom)
        allCards.add(card);

    const CardList played = m_state.playedCards.toCardList(Cards::NoSort);
    inputCount += played.size();
    for (const Card &card : played)
        allCards.add(card);

    // Chaos-mode vanished cards are out of play but still exist; count them so
    // the total stays 54 and duplicate detection remains correct.
    const CardList vanished = m_state.vanishedCards.toCardList(Cards::NoSort);
    inputCount += vanished.size();
    for (const Card &card : vanished)
        allCards.add(card);

    // Check for duplicates
    if (allCards.cardCount() != inputCount)
        return GameError::InvalidDeck;

    // During active or finished dealt round, require 54 cards total
    if (m_state.phase != GamePhase::Waiting && m_state.roundNumber > 0) {
        if (inputCount != 54)
            return GameError::InvalidDeck;
    }

    // Validate seat fields for phase
    if (m_state.phase == GamePhase::CallingLord || m_state.phase == GamePhase::Playing) {
        if (!isValidSeat(m_state.currentSeat))
            return GameError::InvalidSeat;
    }
    if (m_state.phase == GamePhase::RoundFinished) {
        if (!isValidSeat(m_state.winnerSeat))
            return GameError::InvalidSeat;
        if (m_state.currentSeat != kInvalidSeat)
            return GameError::InvalidSeat;
    }

    // Validate roles during Playing/RoundFinished
    if (m_state.phase == GamePhase::Playing || m_state.phase == GamePhase::RoundFinished) {
        int lordCount = 0;
        int farmerCount = 0;
        for (int i = 0; i < kPlayerCount; ++i) {
            if (m_state.players[i].role == PlayerRole::Lord)
                lordCount++;
            else if (m_state.players[i].role == PlayerRole::Farmer)
                farmerCount++;
        }
        if (lordCount != 1 || farmerCount != 2)
            return GameError::InvalidPhase;
    }

    // Validate pending cards are in played cards
    if (!m_state.pendingCards.isEmpty()) {
        if (!m_state.playedCards.contains(m_state.pendingCards))
            return GameError::InvalidPhase;
    }

    // Validate zero-sum scores
    const int scoreSum = m_state.players[0].score + m_state.players[1].score + m_state.players[2].score;
    if (scoreSum != 0)
        return GameError::InvalidPhase;

    return GameError::None;
}

GameState GameEngine::projectedStateFor(int seat) const
{
    // Return empty state for invalid seats
    if (!isValidSeat(seat)) {
        GameState empty;
        return empty;
    }

    GameState projected = m_state;

    // Hide other players' hands
    for (int i = 0; i < kPlayerCount; ++i) {
        if (i != seat) {
            projected.players[i].hand.clear();
        }
    }

    // Hide unrevealed bottom cards (only visible after lord selection)
    if (m_state.phase == GamePhase::Waiting
        || m_state.phase == GamePhase::Dealing
        || m_state.phase == GamePhase::CallingLord) {
        projected.bottomCards.clear();
    }

    return projected;
}
