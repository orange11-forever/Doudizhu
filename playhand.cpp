#include "playhand.h"
#include <QMap>
#include <algorithm>

// ============ 构造 ============

PlayHand::PlayHand()
    : m_type(Hand_Unknown), m_pt(Card::Card_Begin), m_extra(0)
{
}

PlayHand::PlayHand(const Cards &cards)
    : m_type(Hand_Unknown), m_pt(Card::Card_Begin), m_extra(0)
{
    // 标准构造流程：分类 → 判断牌型
    classify(cards);
    judgeCardType();
}

PlayHand::PlayHand(HandType type, Card::CardPoint pt, int extra)
    : m_type(type), m_pt(pt), m_extra(extra)
{
}

// ============ 属性获取 ============

PlayHand::HandType PlayHand::getHandType() const { return m_type; }
Card::CardPoint PlayHand::getCardPoint() const { return m_pt; }
int PlayHand::getExtra() const { return m_extra; }

// ============ 分类：将牌按张数归入对应数组 ============

void PlayHand::classify(const Cards &cards)
{
    // 先得到升序排列的牌列表
    CardList list = cards.toCardList(Cards::Asc);

    // 清空分类数组
    m_oneCard.clear();
    m_twoCard.clear();
    m_threeCard.clear();
    m_fourCard.clear();

    // 统计每种点数的张数
    QMap<Card::CardPoint, int> ptCount;
    for (const auto &c : list)
        ptCount[c.getpoint()]++;

    // 按张数分类
    for (auto it = ptCount.begin(); it != ptCount.end(); ++it)
    {
        switch (it.value())
        {
        case 1:
            m_oneCard.append(it.key());
            break;
        case 2:
            m_twoCard.append(it.key());
            break;
        case 3:
            m_threeCard.append(it.key());
            break;
        case 4:
            m_fourCard.append(it.key());
            break;
        default:
            break;
        }
    }

    // 排序保证顺序一致性
    std::sort(m_oneCard.begin(), m_oneCard.end());
    std::sort(m_twoCard.begin(), m_twoCard.end());
    std::sort(m_threeCard.begin(), m_threeCard.end());
    std::sort(m_fourCard.begin(), m_fourCard.end());
}

// ============ 牌型判断（优先级从低到高依次尝试） ============

void PlayHand::judgeCardType()
{
    // 重置
    m_type = Hand_Unknown;
    m_pt = Card::Card_Begin;
    m_extra = 0;

    // 按牌型优先级依次判断，命中即返回
    if (isPass())
        return;
    if (isSingle())
        return;
    if (isPair())
        return;
    if (isTriple())
        return;
    if (isTripleSingle())
        return;
    if (isTriplePair())
        return;
    if (isPlane())
        return;
    if (isPlaneTwoSingle())
        return;
    if (isPlaneTwoPair())
        return;
    if (isSeqPair())
        return;
    if (isSeqSingle())
        return;
    if (isBomb())
        return;
    if (isBombSingle())
        return;
    if (isBombPair())
        return;
    if (isBombTwoSingle())
        return;
    if (isBombJokers())
        return;
    if (isBombJokersSingle())
        return;
    if (isBombJokersPair())
        return;
    if (isBombJokersTwoSingle())
        return;
}

// ============ 便捷宏：设置牌型和主点数后返回 true ============

#define SET_TYPE_AND_PT(t, p)            \
    do                                   \
    {                                    \
        m_type = t;                      \
        m_pt = p;                        \
        return true;                     \
    } while (0)

// ============ 各牌型判断实现 ============

bool PlayHand::isPass()
{
    // "不出"是一张特殊标记牌：m_oneCard 中仅有一张 Card_Begin
    if (m_oneCard.size() == 1 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
    {
        if (m_oneCard[0] == Card::Card_Begin)
        {
            m_type = Hand_Pass;
            m_pt = Card::Card_Begin;
            return true;
        }
    }
    return false;
}

bool PlayHand::isSingle()
{
    // 单张：恰好1张牌，没有对/三/四
    if (m_oneCard.size() == 1 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
        SET_TYPE_AND_PT(Hand_Single, m_oneCard[0]);
    return false;
}

bool PlayHand::isPair()
{
    // 对子：恰好1个点数出现2次，没有单/三/四
    if (m_oneCard.size() == 0 && m_twoCard.size() == 1 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
        SET_TYPE_AND_PT(Hand_Pair, m_twoCard[0]);
    return false;
}

bool PlayHand::isTriple()
{
    // 三张（不带）：恰好1个点数出现3次
    if (m_oneCard.size() == 0 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 1 && m_fourCard.size() == 0)
        SET_TYPE_AND_PT(Hand_Triple, m_threeCard[0]);
    return false;
}

bool PlayHand::isTripleSingle()
{
    // 三带一：1个三张 + 1个单张
    if (m_oneCard.size() == 1 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 1 && m_fourCard.size() == 0)
        SET_TYPE_AND_PT(Hand_Triple_Single, m_threeCard[0]);
    return false;
}

bool PlayHand::isTriplePair()
{
    // 三带二：1个三张 + 1个对子
    if (m_oneCard.size() == 0 && m_twoCard.size() == 1 &&
        m_threeCard.size() == 1 && m_fourCard.size() == 0)
        SET_TYPE_AND_PT(Hand_Triple_Pair, m_threeCard[0]);
    return false;
}

bool PlayHand::isPlane()
{
    // 飞机（不带翅膀）：≥2个连续的三张，且最大不超过A
    if (m_oneCard.size() == 0 && m_twoCard.size() == 0 &&
        m_threeCard.size() >= 2 && m_fourCard.size() == 0)
    {
        // 检查三张点数是否连续
        for (int i = 1; i < m_threeCard.size(); i++)
        {
            if (m_threeCard[i] != Card::CardPoint(m_threeCard[i - 1] + 1))
                return false;
        }
        // 飞机不能连到2以上（2、小王、大王不能参与飞机）
        if (m_threeCard.last() > Card::Card_A)
            return false;
        // 记录飞机节数：canBeat 要求同类型且 m_extra 相等才比点数，
        // 否则不同长度的纯飞机(如 3 连三 vs 4 连三)会因 m_extra 同为 0 而互压。
        m_extra = m_threeCard.size();
        SET_TYPE_AND_PT(Hand_Plane, m_threeCard.last());
    }
    return false;
}

bool PlayHand::isPlaneTwoSingle()
{
    // 飞机带单：飞机(≥2三连) + 等量单张
    if (m_threeCard.size() >= 2 && m_fourCard.size() == 0)
    {
        int planeCount = m_threeCard.size();
        // 每个翅膀占 1 个单张位：真正的单张各占 1 位，一个对子拆成 2 个单张占 2 位。
        // 原先写成 +m_twoCard.size()（把一对只当 1 位），导致飞机带两对(10张)被误判
        // 成飞机带两单，isPlaneTwoPair 沦为死代码，且 8 张能非法压 10 张。
        int singleCount = m_oneCard.size() + 2 * m_twoCard.size();
        if (planeCount != singleCount)
            return false;

        // 确保三张排序后检查连续性
        std::sort(m_threeCard.begin(), m_threeCard.end());
        for (int i = 1; i < planeCount; i++)
        {
            if (m_threeCard[i] != Card::CardPoint(m_threeCard[i - 1] + 1))
                return false;
        }
        if (m_threeCard.last() > Card::Card_A)
            return false;
        m_extra = planeCount; // 记录飞机节数
        SET_TYPE_AND_PT(Hand_Plane_Two_Single, m_threeCard.last());
    }
    return false;
}

bool PlayHand::isPlaneTwoPair()
{
    // 飞机带双：飞机(≥2三连) + 等量对子，且没有单张
    if (m_threeCard.size() >= 2 && m_oneCard.size() == 0 && m_fourCard.size() == 0)
    {
        int planeCount = m_threeCard.size();
        if (m_twoCard.size() != planeCount)
            return false;

        std::sort(m_threeCard.begin(), m_threeCard.end());
        for (int i = 1; i < planeCount; i++)
        {
            if (m_threeCard[i] != Card::CardPoint(m_threeCard[i - 1] + 1))
                return false;
        }
        if (m_threeCard.last() > Card::Card_A)
            return false;
        m_extra = planeCount;
        SET_TYPE_AND_PT(Hand_Plane_Two_Pair, m_threeCard.last());
    }
    return false;
}

bool PlayHand::isSeqPair()
{
    // 连对：≥3个连续对子，且最大不超过A
    if (m_oneCard.size() == 0 && m_twoCard.size() >= 3 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
    {
        for (int i = 1; i < m_twoCard.size(); i++)
        {
            if (m_twoCard[i] != Card::CardPoint(m_twoCard[i - 1] + 1))
                return false;
        }
        if (m_twoCard.last() > Card::Card_A)
            return false;
        m_extra = m_twoCard.size(); // 记录连对长度
        SET_TYPE_AND_PT(Hand_Seq_Pair, m_twoCard.last());
    }
    return false;
}

bool PlayHand::isSeqSingle()
{
    // 顺子：≥5个连续单张，且最大不超过A
    if (m_oneCard.size() >= 5 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
    {
        for (int i = 1; i < m_oneCard.size(); i++)
        {
            if (m_oneCard[i] != Card::CardPoint(m_oneCard[i - 1] + 1))
                return false;
        }
        if (m_oneCard.last() > Card::Card_A)
            return false;
        m_extra = m_oneCard.size(); // 记录顺子长度
        SET_TYPE_AND_PT(Hand_Seq_Single, m_oneCard.last());
    }
    return false;
}

bool PlayHand::isBomb()
{
    // 炸弹：恰好4张同点数
    if (m_oneCard.size() == 0 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 1)
        SET_TYPE_AND_PT(Hand_Bomb, m_fourCard[0]);
    return false;
}

bool PlayHand::isBombSingle()
{
    // 四带二单：1个四张 + 2个单张（共6张牌）
    if (m_oneCard.size() == 2 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 1)
        SET_TYPE_AND_PT(Hand_Bomb_Single, m_fourCard[0]);
    return false;
}

bool PlayHand::isBombPair()
{
    // 四带两对：1个四张 + 2个对子（共8张牌）
    if (m_oneCard.size() == 0 && m_twoCard.size() == 2 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 1)
        SET_TYPE_AND_PT(Hand_Bomb_Pair, m_fourCard[0]);
    return false;
}

bool PlayHand::isBombTwoSingle()
{
    // 已停用：原实现接受 4张 + 1单 + 1对 = 7 张，这是标准斗地主中不存在的牌型。
    // 合法的"四带二"只有两种：四带两单(4+2单=6张，见 isBombSingle) 与
    // 四带两对(4+2对=8张，见 isBombPair)。保留 Hand_Bomb_Two_Single 枚举仅供
    // 音效映射引用，但不再产生该牌型，避免非法 7 张牌被判为合法可出。
    return false;
}

bool PlayHand::isBombJokers()
{
    // 火箭(王炸)：大小王各一张
    if (m_oneCard.size() == 2 && m_twoCard.size() == 0 &&
        m_threeCard.size() == 0 && m_fourCard.size() == 0)
    {
        if (m_oneCard.contains(Card::Card_SJ) && m_oneCard.contains(Card::Card_BJ))
            SET_TYPE_AND_PT(Hand_Bomb_Jokers, Card::Card_BJ);
    }
    return false;
}

// 以下为王炸变体——当前版本未启用（仅保留接口）
bool PlayHand::isBombJokersSingle() { return false; }
bool PlayHand::isBombJokersPair() { return false; }
bool PlayHand::isBombJokersTwoSingle() { return false; }

#undef SET_TYPE_AND_PT

// ============ 比较大小 ============

bool PlayHand::canBeat(const PlayHand &other) const
{
    // 未知牌型不能比较
    if (m_type == Hand_Unknown || other.m_type == Hand_Unknown)
        return false;

    // 火箭无敌，除非对面也是火箭
    if (m_type == Hand_Bomb_Jokers)
        return true;
    if (other.m_type == Hand_Bomb_Jokers)
        return false;

    // 炸弹可压制非炸弹，炸弹间比点数
    bool iAmBomb = (m_type == Hand_Bomb);
    bool otherBomb = (other.m_type == Hand_Bomb);

    if (iAmBomb && !otherBomb)
        return true; // 炸弹＞非炸弹
    if (!iAmBomb && otherBomb)
        return false;              // 非炸弹 < 炸弹
    if (iAmBomb && otherBomb)
        return m_pt > other.m_pt;  // 炸弹之间比点数

    // 同类型比点数，且附加信息（顺子长度等）必须一致
    if (m_type != other.m_type)
        return false;
    if (m_extra != other.m_extra)
        return false;
    return m_pt > other.m_pt;
}

// ============ 牌型名称 ============

QString PlayHand::handTypeName(HandType type)
{
    switch (type)
    {
    case Hand_Pass:
        return QStringLiteral("不出");
    case Hand_Single:
        return QStringLiteral("单张");
    case Hand_Pair:
        return QStringLiteral("对子");
    case Hand_Triple:
        return QStringLiteral("三张");
    case Hand_Triple_Single:
        return QStringLiteral("三带一");
    case Hand_Triple_Pair:
        return QStringLiteral("三带二");
    case Hand_Plane:
        return QStringLiteral("飞机");
    case Hand_Plane_Two_Single:
        return QStringLiteral("飞机带单");
    case Hand_Plane_Two_Pair:
        return QStringLiteral("飞机带双");
    case Hand_Seq_Pair:
        return QStringLiteral("连对");
    case Hand_Seq_Single:
        return QStringLiteral("顺子");
    case Hand_Bomb:
        return QStringLiteral("炸弹");
    case Hand_Bomb_Single:
        return QStringLiteral("炸弹带单");
    case Hand_Bomb_Pair:
        return QStringLiteral("炸弹带对");
    case Hand_Bomb_Two_Single:
        return QStringLiteral("炸弹带两单");
    case Hand_Bomb_Jokers:
        return QStringLiteral("火箭");
    default:
        return QStringLiteral("未知");
    }
}
