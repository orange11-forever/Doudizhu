#ifndef GAMEPANEL_H
#define GAMEPANEL_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMap>
#include <QLineEdit>
#include <QComboBox>
#include"cardpanel.h"
#include "card.h"
#include "cards.h"
#include "gamecontrol_adapter.h"
#include "musicplayer.h"
#include "chatpanel.h"
#include "relay_client.h"
#include "game_core/remote_game_session.h"
#include "theme_manager.h"
#include "chaos_engine.h"
#include "sticker_store.h"
#include "avatar_store.h"

class CardPanel;
class QWidget;
class QMovie;

QT_BEGIN_NAMESPACE
namespace Ui { class GamePanel; }
QT_END_NAMESPACE

/**
 * @brief 游戏主窗口 —— 斗地主 GUI 入口
 *
 * 层次结构（从底到顶）：
 * - centralWidget → m_bgLabel(背景) → m_cardLayer(卡牌+按钮统一层)
 *
 * 核心功能：
 * - 主菜单（人机对战 / 创建房间 / 加入房间）
 * - 卡牌渲染与管理（用户手牌、机器人手牌、出牌区、底牌区）
 * - 游戏操作（出牌/不出/提示/叫分）集成倒计时
 * - 出牌动画（机器人出牌的缩放+淡入效果）
 * - 网络联机（Relay 客户端 + 服务器权威路径）
 * - 聊天面板 + 背景音乐/音效
 */
class GamePanel : public QMainWindow
{
    Q_OBJECT

public:
    explicit GamePanel(QWidget *parent = nullptr);
    ~GamePanel() override;

protected:
    /// 窗口尺寸变化时重排 UI 和背景
    void resizeEvent(QResizeEvent *event) override;

private slots:
    // ============ 菜单按钮 ============
    void onVsAI();
    void onCreateRoom();
    void onJoinRoom();

    // ============ 游戏流程控制 ============
    void onStartGame();
    void onRestartGame();
    void onBackToMenu();

    // ============ 游戏状态回调 ============
    void onGameStatusChanged(GameControlAdapter::GameStatus status);
    void onPlayerStatusChanged(Player *player, GameControlAdapter::PlayerStatus status);
    void onNotifyCallLord(Player *player, int bet, bool isFirst);
    void onNotifyPlayHand(Player *player, const Cards &cards);

    // ============ 用户操作 ============
    void onCardSelected(int cardId);   ///< 选牌
    void onBtnPlay();                  ///< 出牌
    void onBtnPass();                  ///< 不出
    void onBtnHint();                  ///< 提示
    void onBtnCallLord(int bet);       ///< 叫地主

    // ============ 倒计时 ============
    void onCountdownTick();

    // ============ 聊天 ============
    void onChatSend(const QString &msg);

    // ============ 网络回调 ============
    void onServerLog(const QString &msg);
    void onClientConnected();
    void onClientDisconnected();
    void onClientLoginSuccess(int playerId);

    // ============ Relay 回调 ============
    void onRelayConnected();
    void onRelayDisconnected();
    void onRoomCreated(quint32 roomId);
    void onRoomJoined(quint32 roomId, int myPid, const QList<RemotePlayer> &players);
    void onRoomJoinFailed(const QString &reason);
    void onRemotePlayerJoined(int playerId, const QString &name, const QString &avatarId);
    void onRemotePlayerLeft(int playerId);
    void onRoomDissolved(const QString &reason);
    void onRelayGameStarted();
    void onRelayGameMessage(quint8 subType, const QByteArray &payload);
    void onRelayError(const QString &error);

    // ============ 服务器权威事件（RemoteGameSession） ============
    void onRemoteGameEvent(const GameEvent &event);
    void onRemoteCommandRejected(GameError error);
void onChaosMode();
    void onAvatarSelect();
    void onChaosStickerPopup(const QPixmap &sticker, QPoint pos, int durationMs);
    void onChaosShake(int intensity);


private:
    // 卡牌尺寸常量（跨 gamepanel*.cpp 多个翻译单元共用，放头文件避免 ODR 问题）
    static constexpr int CARD_W      = 75;
    static constexpr int CARD_HEIGHT = 107;
    static constexpr int OVERLAP     = 26;

    // ============ 初始化 ============
    void initUI();           ///< 创建所有 UI 控件
    void initConnections();  ///< 连接 GameControl 的信号
    void initGame();         ///< 创建 GameControl、MusicPlayer 等
    void initBackground();   ///< 加载并设置全屏背景图
    void applyBackgroundScaled();///< 按当前窗口尺寸等比铺满背景（初始 + resize 共用）

    // ============ 模式切换 ============
    void enterGameMode();    ///< 进入游戏模式
    void enterMenuMode();    ///< 进入主菜单模式

    // ============ 卡牌渲染 ============
    void updateAllPlayerCards();              ///< 刷新所有玩家手牌显示
    void updateUserCards();                   ///< 刷新用户手牌
    void updateRobotCards(Player *robot);     ///< 刷新机器人手牌（最多显示6张背面）
    void updatePlayArea(Player *player, const Cards &cards); ///< 刷新出牌区（带动画）
    void updateLordCards();                   ///< 刷新底牌区
    void updateLordIcons();                   ///< 刷新地主图标
    void clearPlayArea();                     ///< 清空出牌区
    void clearLordCards();                    ///< 清空底牌区

    // ============ 布局编排 ============
    void arrangeUserCards();                  ///< 排列用户手牌（选中上移15px）
    void arrangeRobotCards(Player *robot);    ///< 排列机器人手牌（背面堆叠）
    void arrangePlayArea();                   ///< 排列出牌区
    void arrangeLordArea();                   ///< 排列底牌区
    void arrangeMenuButtons();                ///< 排列主菜单按钮

    // ============ 按钮显隐 ============
    void setCallButtonsVisible(bool v);   ///< 叫地主按钮
    void setPlayButtonsVisible(bool v);   ///< 出牌/不出/提示按钮
    void setMenuButtonsVisible(bool v);   ///< 主菜单按钮
    void raiseGameButtons();              ///< 确保游戏按钮在最上层（不被卡牌遮挡）

    /// 在 gamecontrol 中查找对应的实际 Player 指针
    Player *findPlayerPtr(Player *p);
    int localRelayPlayerId() const;
    Player *playerForRelayId(int playerId) const;
    int relayIdForPlayer(Player *player) const;
    QString relayNameForPlayerId(int playerId) const;
    void configureOnlinePlayers();
    void wireRelayClient();                   ///< 幂等创建 RelayClient 并连接其信号（建房/加入共用）
    void resetOnlineState();
    void resetThemeForOnline();               ///< 进房时卸载单机主题/混沌，避免残留漏进联机
    void updateRoomStartVisibility();
    void ensureRemoteSession();               ///< 创建并连接 RemoteGameSession（幂等）
    void applyRemoteTurnUI(Player *current);  ///< 依服务器视图阶段刷新当前玩家的操作 UI
    int remoteHandCountFor(Player *player) const; ///< 联机模式下某玩家剩余牌数（来自服务器视图）
    void renderAvatarTo(QLabel *label, const QString &avatarId, const QColor &ring); ///< 将头像绘成圆形贴到座位标签

    Ui::GamePanel *ui;

    // ============ 背景 ============
    QLabel *m_bgLabel;      ///< 全屏背景 Label
    QPixmap m_bgSource;     ///< 原始背景图（用于 resize 缩放裁剪）

    // ============ 核心组件 ============
    GameControlAdapter *m_gameControl;   ///< 游戏逻辑控制器（新架构）
    MusicPlayer *m_musicPlayer;          ///< 音乐/音效播放器
    RelayClient *m_relayClient = nullptr; ///< Relay 客户端（新联机模式）
    RemoteGameSession *m_remoteSession = nullptr; ///< 服务器权威会话（视图端）
    bool m_isOnlineMode = false;          ///< 是否联机模式
    bool m_isHost = false;                ///< 当前客户端是否为房主

    QPixmap m_cardBack;  ///< 牌背面图片（共用）

    // ============ 用户手牌 ============
    QVector<CardPanel *> m_userCardPanels; ///< 手牌控件列表
    QVector<Card> m_userCards;             ///< 手牌数据列表
    QSet<int> m_selectedIds;               ///< 当前选中的手牌索引

    // ============ 出牌区 ============
    QVector<CardPanel *> m_playAreaPanels; ///< 出牌区控件
    QLabel *m_playLabel;                   ///< "不出" 文字提示

    // ============ 底牌区 ============
    QVector<CardPanel *> m_lordCardPanels; ///< 底牌控件

    // ============ 机器人手牌 ============
    QVector<CardPanel *> m_leftRobotCards;  ///< 左机器人手牌（背面）
    QVector<CardPanel *> m_rightRobotCards; ///< 右机器人手牌（背面）

    // ============ 层级容器 ============
    QWidget *m_cardLayer;    ///< 卡牌层（背景之上，按钮和卡牌均挂载于此层）

    // ============ 玩家信息面板 ============
    QLabel *m_leftNameLabel;
    QLabel *m_rightNameLabel;
    QLabel *m_userNameLabel;
    QLabel *m_userAvatar;      QLabel *m_leftAvatar;    QLabel *m_rightAvatar;
    QLabel *m_userLordIcon;    QLabel *m_leftLordIcon;  QLabel *m_rightLordIcon;

    // ============ 主菜单按钮 ============
    QPushButton *m_btnVsAI;
    QPushButton *m_btnCreateRoom;
    QPushButton *m_btnJoinRoom;

    // ============ 游戏操作按钮 ============
    QPushButton *m_btnStart;
    QPushButton *m_btnRestart;
    QPushButton *m_btnBackMenu;
    QPushButton *m_btnPlay;
    QPushButton *m_btnPass;
    QPushButton *m_btnHint;
    QPushButton *m_btnCall1;   ///< 叫1分
    QPushButton *m_btnCall2;   ///< 叫2分
    QPushButton *m_btnCall3;   ///< 叫3分
    QPushButton *m_btnNoCall;  ///< 不叫

    // ============ 界面辅助 ============
    QLabel *m_timerLabel;          ///< 倒计时标签
    QTimer *m_countdownTimer;      ///< 倒计时定时器（1秒间隔）
    int m_countdownSec;            ///< 剩余秒数
    QLabel *m_statusLabel;         ///< 状态文本（"叫地主阶段"/"请出牌"等）
    QLabel *m_titleLabel;          ///< 菜单标题 "🃏 斗 地 主"
    QLabel *m_titleLogo = nullptr; ///< 标题左侧 logo（images/logo/title.gif 动图优先，否则 title.png）
    QMovie *m_titleMovie = nullptr;///< 标题 logo 动图（GIF）播放器；静态图时为空
    bool m_hasTitleLogo = false;   ///< 是否成功加载到标题 logo（动图或静态图），驱动显隐/布局
    ChatPanel *m_chatPanel;        ///< 聊天面板
    QLabel *m_roomIdLabel;      ///< 房间号显示（房主可见）
    QLabel *m_playerListLabel;  ///< 房间内玩家列表
    int m_remotePlayers = 0;    ///< 联机模式房间人数
    quint32 m_pendingRoomId = 0; ///< 暂存要加入的房间号
    Cards m_relayLordCards;     ///< Relay 加入方渲染用底牌
// ============ 主题/混沌/头像系统 ============
    ThemeManager *m_themeManager;
    ChaosEngine *m_chaosEngine;
    StickerStore *m_stickerStore;
    AvatarStore *m_avatarStore;
    QString m_playerAvatarId;
    QString m_leftBotAvatarId;
    QString m_rightBotAvatarId;

    // ============ 主题选择 UI ============
    QPushButton *m_btnChaosMode;
    QPushButton *m_btnAvatar;
    QPushButton *m_btnConfirmTheme;
    QComboBox *m_themeCombo;
    QLabel *m_chaosBanner;
    QStringList m_themeNames;
    int m_passStreak = 0;

};

#endif // GAMEPANEL_H
