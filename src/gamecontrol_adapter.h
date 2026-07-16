#ifndef GAMECONTROL_ADAPTER_H
#define GAMECONTROL_ADAPTER_H

#include <QObject>
#include "game_core/local_session.h"
#include "player.h"
#include "robot.h"
#include "userplayer.h"

class ChaosEngine;

/**
 * @class GameControlAdapter
 * @brief GameControl适配器 - 将LocalSession桥接到旧GameControl API
 *
 * ## 设计目标
 *
 * GameControlAdapter是迁移到LocalSession架构的关键适配层，实现了：
 * 1. **API兼容性**: 提供与旧GameControl完全相同的公共接口
 * 2. **零破坏性迁移**: GamePanel无需修改即可使用新架构
 * 3. **事件转换**: 将GameEvent转换为旧的Qt信号
 * 4. **Player包装**: 维护Player对象作为数据容器，保持UI兼容
 *
 * ## 架构层次
 *
 * ```
 * GamePanel (UI层)
 *     ↓ 调用旧API
 * GameControlAdapter (适配层) ← 你在这里
 *     ↓ 委托给
 * LocalSession (会话管理)
 *     ↓ 使用
 * GameEngine (核心逻辑)
 * ```
 *
 * ## 迁移策略
 *
 * **保留的**:
 * - 所有公共API方法签名
 * - Player/Robot/UserPlayer对象
 * - 所有信号（名称和参数）
 * - GameStatus/PlayerStatus枚举
 *
 * **替换的**:
 * - 游戏逻辑：从GameControl内部实现 → LocalSession + GameEngine
 * - AI决策：从Robot::thinkXxx() → Strategy类
 * - 状态管理：从可变状态 → 不可变GameState
 *
 * **隔离的**:
 * - 混沌模式（ChaosEngine）：暂时保留旧逻辑
 * - Player信号：仅作为通知机制，不再驱动逻辑
 *
 * ## 使用示例
 *
 * ```cpp
 * // GamePanel代码无需修改
 * GameControlAdapter *gc = new GameControlAdapter(this);
 * gc->playerInit();
 *
 * connect(gc, &GameControlAdapter::gameStatusChanged,
 *         this, &GamePanel::onGameStatusChanged);
 *
 * // 开始游戏
 * gc->resetCardData();
 * gc->startLordCard();
 *
 * // 用户出牌
 * gc->onPlayHand(userPlayer, cards);
 * ```
 *
 * ## 已知限制
 *
 * - Relay模式绕过LocalSession（技术债TD-004）
 * - 混沌模式不与GameState同步（技术债TD-005）
 * - Player对象状态可能与GameState短暂不一致
 *
 * @see LocalSession, GameEngine, GamePanel
 */
class GameControlAdapter : public QObject
{
    Q_OBJECT

public:
    // ============ GameControl兼容的公共接口 ============

    enum GameStatus {
        DispatchCard,
        CallingLord,
        PlayingHand
    };

    enum PlayerStatus {
        ThinkingForCallLord,
        PickingCard,
        Playing,
        ThinkingForPlayHand,  // Add this for Relay compatibility
        Winning
    };

    explicit GameControlAdapter(QObject *parent = nullptr);
    ~GameControlAdapter();

    // ============ 游戏控制接口 ============

    void playerInit();
    void resetCardData();
    void startLordCard();
    void becomeLord(Player *player, int bet);
    void disable();

    // ============ 信号处理 ============

    void onGrabBet(Player *player, int bet);
    void onPlayHand(Player *player, const Cards &card);

    /// 单机模式下用户输入直接驱动 LocalSession 引擎；联机模式（host 权威转发 /
    /// joiner 服务器转发）走各自网络路径，须关闭以免与引擎双路径冲突。默认开启。
    void setUserInputDrivesEngine(bool enabled) { m_userInputDrivesEngine = enabled; }

    // ============ Player访问器 ============

    Robot *getLeftRobot() const { return m_robotLeft; }
    Robot *getRightRobot() const { return m_robotRight; }
    UserPlayer *getUserPlayer() const { return m_user; }
    Player *getCurrentPlayer() const { return m_currPlayer; }
    Player *getPendPlayer() const { return m_pendPlayer; }
    Cards getPendCards() const { return m_pendCards; }

    // ============ 混沌模式支持 ============

    void setChaosEngine(ChaosEngine *engine) { m_chaosEngine = engine; }
    ChaosEngine *chaosEngine() const { return m_chaosEngine; }

    /// 把混沌改牌参数下发给底层 LocalSession（决策留会话、执行走引擎、权威状态唯一真相）。
    void setChaosParams(bool active, double disappearChance, double transformChance,
                        bool disappearOnDraw, bool transformOnPlay, bool disappearOnIdle);
    /// 玩家出牌变形入口：与 AI 共用会话层 maybeTransformPlay，命中发 chaosCardsTransformed。
    Cards maybeTransformPlayerCards(const Cards &sel);
    /// 发呆超时消牌（掷骰+执行都在会话）：真的消掉一张返回 true，供 UI 显示横幅。
    bool tryIdleVanishFor(Player *player);

    // ============ 牌堆操作 ============

    Cards getSurplusCards() const;  // For Relay mode: returns lord cards

    // ============ Relay模式支持 ============

    void setCurrentPlayer(Player *player);
    void syncPendingInfo(Player *player, const Cards &cards);

signals:
    void playerStatusChanged(Player *player, PlayerStatus status);
    void notifyGrabLordBet(Player *player, int bet, bool flag);
    void gameStatusChanged(GameStatus status);
    void notifyPlayHand(Player *player, const Cards &card);
    void pendingInfo(Player *player, const Cards &card);
    void chaosCardDisappeared(Player *player, const Card &card);
    /// 混沌出牌变形已发生（AI 座位；玩家变形在 GamePanel 本地处理）。转发自 LocalSession。
    void chaosCardsTransformed(Player *player, const Card &from, const Card &to);

private slots:
    void onSessionEvent(const GameEvent &event);
    void onSessionRoundFinished(int winnerSeat);

private:
    void setupConnections();
    void emitEventAsSignals(const GameEvent &event);
    void syncStateToPlayers();
    Player *seatToPlayer(int seat) const;
    int playerToSeat(Player *player) const;

    LocalSession *m_session;
    Robot *m_robotLeft;
    Robot *m_robotRight;
    UserPlayer *m_user;
    Player *m_currPlayer;
    Player *m_pendPlayer;
    Cards m_pendCards;
    ChaosEngine *m_chaosEngine;
    bool m_sessionActive;
    bool m_userInputDrivesEngine = true;
};

#endif // GAMECONTROL_ADAPTER_H
