#include "game_serialization.h"

namespace GameSerialization
{

// 卡牌码：point*10 + suit（与 NetMsg::encodeCard 保持一致）
static quint8 encodeCard(const Card &c)
{
    return static_cast<quint8>(c.getpoint() * 10 + c.getsuit());
}

static Card decodeCard(quint8 code)
{
    const int point = code / 10;
    const int suit = code % 10;

    // Validate against real enum ranges before casting. Corrupt/malicious codes
    // otherwise produce out-of-range enums that overflow suitBase[] in the
    // renderer. Jokers carry suit 0 (Suit_Begin); normal cards suit 1..4.
    const bool joker = (point == Card::Card_SJ || point == Card::Card_BJ) && suit == Card::Suit_Begin;
    const bool normal = (point >= Card::Card_3 && point <= Card::Card_2)
                        && (suit >= Card::Diamond && suit <= Card::Spade);
    if (!joker && !normal)
        return Card();  // invalid marker (default-constructed)

    return Card(static_cast<Card::CardPoint>(point),
                static_cast<Card::CardSuit>(suit));
}

void writeCards(QDataStream &out, const Cards &cards)
{
    const CardList list = cards.toCardList(Cards::Asc);
    out << static_cast<quint8>(list.size());
    for (const Card &c : list)
        out << encodeCard(c);
}

Cards readCards(QDataStream &in)
{
    Cards cards;
    quint8 count = 0;
    in >> count;
    // 截断/损坏包防护(N5)：读 count 后若流已异常，直接返回空，绝不按声称的
    // count 继续循环(否则截断包能让我们空转最多 255 次、塞入默认牌)。
    if (in.status() != QDataStream::Ok)
        return Cards();
    for (quint8 i = 0; i < count; ++i) {
        quint8 code = 0;
        in >> code;
        // 每张读取后校验流状态：一旦读过界立即停，丢弃残缺尾部，返回已读到的合法牌。
        if (in.status() != QDataStream::Ok)
            break;
        cards.add(decodeCard(code));
    }
    return cards;
}

void writeCommand(QDataStream &out, const GameCommand &command)
{
    out << static_cast<quint8>(command.type)
        << static_cast<qint32>(command.seat)
        << static_cast<qint32>(command.bid);
    writeCards(out, command.cards);
}

GameCommand readCommand(QDataStream &in)
{
    GameCommand command;
    quint8 type = 0;
    qint32 seat = kInvalidSeat;
    qint32 bid = 0;
    in >> type >> seat >> bid;
    // 头部字段读坏 → 返回一个座位非法的默认命令，引擎的座位校验会安全拒掉它。
    if (in.status() != QDataStream::Ok) {
        command.seat = kInvalidSeat;
        return command;
    }
    command.type = static_cast<GameCommandType>(type);
    command.seat = seat;
    command.bid = bid;
    command.cards = readCards(in);
    return command;
}

void writeEvent(QDataStream &out, const GameEvent &event)
{
    out << static_cast<quint8>(event.type)
        << static_cast<qint32>(event.seat)
        << static_cast<qint32>(event.value);
    writeCards(out, event.cards);
}

GameEvent readEvent(QDataStream &in)
{
    GameEvent event;
    quint8 type = 0;
    qint32 seat = kInvalidSeat;
    qint32 value = 0;
    in >> type >> seat >> value;
    // 头部字段读坏 → 返回默认(RoundStarted, 座位非法)事件；RemoteGameSession 的
    // applyEvent 对每个分支都有 isValidSeat 守卫，不会因此越界写。
    if (in.status() != QDataStream::Ok) {
        event.seat = kInvalidSeat;
        return event;
    }
    event.type = static_cast<GameEventType>(type);
    event.seat = seat;
    event.value = value;
    event.cards = readCards(in);
    return event;
}

} // namespace GameSerialization
