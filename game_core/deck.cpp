#include "deck.h"

#include "cards.h"

#include <algorithm>
#include <random>

CardList Deck::standard()
{
    CardList cards;
    cards.reserve(54);
    for (int point = Card::Card_3; point <= Card::Card_2; ++point) {
        for (int suit = Card::Diamond; suit <= Card::Spade; ++suit) {
            cards.append(Card(static_cast<Card::CardPoint>(point),
                              static_cast<Card::CardSuit>(suit)));
        }
    }
    cards.append(Card(Card::Card_SJ, Card::Suit_Begin));
    cards.append(Card(Card::Card_BJ, Card::Suit_Begin));
    return cards;
}

CardList Deck::shuffled(quint32 seed)
{
    CardList cards = standard();
    std::mt19937 generator(seed);
    std::shuffle(cards.begin(), cards.end(), generator);
    return cards;
}

bool Deck::isValid(const CardList &cards)
{
    if (cards.size() != 54)
        return false;

    Cards actual;
    for (const Card &card : cards)
        actual.add(card);

    Cards expected;
    for (const Card &card : standard())
        expected.add(card);
    return actual == expected;
}
