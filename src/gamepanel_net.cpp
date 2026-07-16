// ===================================================================
// gamepanel_net.cpp —— GamePanel 的网络 / Relay 联机层
//
// 从 gamepanel.cpp 拆出（God Class 重构第二步）。这里承担联机相关：
//  - 旧 GameServer/GameClient 状态回调（onServerLog/onClient*）
//  - Relay 房间生命周期（onRelay*/onRoom*/onRemotePlayer*）
//  - 服务器权威事件 → UI（onRemoteGameEvent/onRemoteCommandRejected/
//    applyRemoteTurnUI）
//  - relay playerId ↔ 本地 Player* 座位映射与在线状态管理
//
// 与 gamepanel.cpp / gamepanel_render.cpp 同属 GamePanel 类实现，共用
// gamepanel.h 的成员。渲染动作（updateAllPlayerCards 等）在 render TU。
// ===================================================================

#include "gamepanel.h"
#include "playhand.h"
#include "relay_game_mapping.h"

#include <QDataStream>
#include <QMessageBox>
#include <QTimer>
#include <QColor>

// ============ 网络回调 ============

void GamePanel::onServerLog(const QString &msg)       { m_chatPanel->addSystemMessage(msg); }
void GamePanel::onClientConnected()                    { m_chatPanel->addSystemMessage(QStringLiteral("🔗 已连接")); }
void GamePanel::onClientDisconnected()                 { m_chatPanel->addSystemMessage(QStringLiteral("🔌 断开连接")); }
void GamePanel::onClientLoginSuccess(int id)           { m_chatPanel->addSystemMessage(QStringLiteral("✅ 登录成功 (ID:%1)").arg(id)); }

void GamePanel::onRelayConnected()
{
    m_chatPanel->addSystemMessage(QStringLiteral("✅ 已连接到服务器"));

    if (m_isHost)
    {
        m_relayClient->createRoom(m_userNameLabel->text(), m_playerAvatarId);
    }
    else
    {
        m_relayClient->joinRoom(m_pendingRoomId, m_userNameLabel->text(), m_playerAvatarId);
        m_statusLabel->setText(QStringLiteral("正在加入房间 %1...").arg(m_pendingRoomId));
    }
}

void GamePanel::onRelayDisconnected()
{
    m_chatPanel->addSystemMessage(QStringLiteral("🔌 与服务器断开连接"));
    resetOnlineState();
}

void GamePanel::onRoomCreated(quint32 roomId)
{
    m_roomIdLabel->setText(QStringLiteral("房间号: %1").arg(roomId, 6, 10, QChar('0')));
    m_roomIdLabel->show();
    m_roomIdLabel->adjustSize();
    m_roomIdLabel->move(width() / 2 - m_roomIdLabel->width() / 2, 60);

    m_statusLabel->setText(QStringLiteral("🏠 房间已创建 — 等待玩家加入"));
    m_chatPanel->addSystemMessage(
        QStringLiteral("🏠 房间创建成功! 房间号: %1 — 告诉朋友输入此号码加入")
            .arg(roomId, 6, 10, QChar('0')));

    // 房主 playerId=1 → 引擎座位 0。建立服务器权威会话。
    ensureRemoteSession();
    if (m_remoteSession)
        m_remoteSession->setLocalSeat(0);

    // 渲染房主自己的头像（与发给对手的 avatarId 保持一致，未手动选过也显示默认头像而非 emoji）。
    renderAvatarTo(m_userAvatar, m_playerAvatarId, QColor(0, 255, 255));

    updateRoomStartVisibility();
}

void GamePanel::onRoomJoined(quint32 roomId, int myPid, const QList<RemotePlayer> &players)
{
    m_remotePlayers = players.size();
    m_statusLabel->setText(QStringLiteral("✅ 已加入房间 %1").arg(roomId, 6, 10, QChar('0')));

    // 加入方 playerId=myPid → 引擎座位 myPid-1。建立服务器权威会话。
    ensureRemoteSession();
    if (m_remoteSession)
        m_remoteSession->setLocalSeat(myPid - 1);

    QStringList names;
    for (const auto &p : players)
    {
        names << p.name;
        if (Player *player = playerForRelayId(p.playerId))
        {
            player->setName(p.name);
            if (player == m_gameControl->getUserPlayer())
            {
                m_userNameLabel->setText(p.name);
                renderAvatarTo(m_userAvatar, p.avatarId, QColor(0, 255, 255));
            }
            else if (player == m_gameControl->getRightRobot())
            {
                m_rightNameLabel->setText(p.name);
                renderAvatarTo(m_rightAvatar, p.avatarId, QColor(255, 215, 0));
            }
            else if (player == m_gameControl->getLeftRobot())
            {
                m_leftNameLabel->setText(p.name);
                renderAvatarTo(m_leftAvatar, p.avatarId, QColor(255, 215, 0));
            }
        }
    }
    m_chatPanel->addSystemMessage(
        QStringLiteral("✅ 加入成功! ID:%1  房间内: %2").arg(myPid).arg(names.join(", ")));

    m_playerListLabel->setText(QStringLiteral("👥 ") + names.join("  "));
    m_playerListLabel->adjustSize();
    m_playerListLabel->move(width() / 2 - m_playerListLabel->width() / 2, 120);
    m_playerListLabel->show();

    updateRoomStartVisibility();
}

void GamePanel::onRoomJoinFailed(const QString &reason)
{
    m_statusLabel->setText(QStringLiteral("❌ 加入失败: %1").arg(reason));
    m_chatPanel->addSystemMessage(QStringLiteral("❌ 加入失败: %1").arg(reason));
    resetOnlineState();
}

void GamePanel::onRemotePlayerJoined(int playerId, const QString &name, const QString &avatarId)
{
    m_remotePlayers++;
    m_chatPanel->addSystemMessage(QStringLiteral("👤 %1 加入房间").arg(name));
    m_statusLabel->setText(QStringLiteral("👤 %1 加入了房间 (%2/3)").arg(name).arg(m_remotePlayers));

    if (Player *player = playerForRelayId(playerId))
    {
        player->setName(name);
        if (player == m_gameControl->getRightRobot())
        {
            m_rightNameLabel->setText(name);
            renderAvatarTo(m_rightAvatar, avatarId, QColor(255, 215, 0));
        }
        else if (player == m_gameControl->getLeftRobot())
        {
            m_leftNameLabel->setText(name);
            renderAvatarTo(m_leftAvatar, avatarId, QColor(255, 215, 0));
        }
    }

    QString current = m_playerListLabel->text();
    m_playerListLabel->setText(current + "  " + name);
    m_playerListLabel->adjustSize();
    m_playerListLabel->move(width() / 2 - m_playerListLabel->width() / 2, 120);
    updateRoomStartVisibility();
}

void GamePanel::onRemotePlayerLeft(int playerId)
{
    if (Player *player = playerForRelayId(playerId))
    {
        player->setName(QStringLiteral("等待中..."));
        if (player == m_gameControl->getRightRobot())
            m_rightNameLabel->setText(QStringLiteral("等待中..."));
        else if (player == m_gameControl->getLeftRobot())
            m_leftNameLabel->setText(QStringLiteral("等待中..."));
    }

    m_remotePlayers = qMax(1, m_remotePlayers - 1);
    m_chatPanel->addSystemMessage(QStringLiteral("👋 玩家离开，当前 %1/3").arg(m_remotePlayers));
    m_statusLabel->setText(QStringLiteral("👋 玩家离开 (%1/3)").arg(m_remotePlayers));
    updateRoomStartVisibility();
}

void GamePanel::onRoomDissolved(const QString &reason)
{
    m_chatPanel->addSystemMessage(QStringLiteral("🚫 房间解散: %1").arg(reason));
    m_statusLabel->setText(QStringLiteral("🚫 房间已解散"));
    resetOnlineState();
}

void GamePanel::onRelayGameStarted()
{
    m_chatPanel->addSystemMessage(QStringLiteral("🎮 游戏开始!"));
    if (!m_isHost)
    {
        m_statusLabel->setText(QStringLiteral("游戏开始，等待发牌..."));
    }
}

void GamePanel::onRelayGameMessage(quint8 subType, const QByteArray &payload)
{
    // 服务器权威模式：SvrEvent/SvrRejected 交给 RemoteGameSession 反序列化并
    // 更新只读视图，再经其信号回到 onRemoteGameEvent/onRemoteCommandRejected 刷 UI。
    // 聊天不经引擎，直接在此渲染。
    switch (subType)
    {
    case NetMsg::GameSub::SvrEvent:
    case NetMsg::GameSub::SvrRejected:
        if (m_remoteSession)
            m_remoteSession->handleServerMessage(subType, payload);
        break;

    case NetMsg::GameSub::ChatBroadcast:
    {
        QDataStream in(payload);
        in.setVersion(QDataStream::Qt_6_0);
        qint32 playerId;
        QString msg;
        in >> playerId >> msg;
        // 贴图标记 [[STK:N]]：渲染成表情图；否则按普通文本显示。
        if (msg.startsWith(QStringLiteral("[[STK:")) && msg.endsWith(QStringLiteral("]]")))
        {
            bool ok = false;
            const int idx = msg.mid(6, msg.size() - 8).toInt(&ok);
            QPixmap pix = ok ? m_stickerStore->get(idx) : QPixmap();
            if (!pix.isNull())
            {
                m_chatPanel->addStickerMessage(relayNameForPlayerId(playerId), pix);
                break;
            }
            // 序号非法/表情缺失：退回文本，避免静默丢消息。
        }
        m_chatPanel->addMessage(relayNameForPlayerId(playerId), msg);
        break;
    }

    default:
        break;
    }
}

// ===================================================================
// 服务器权威事件 → UI（RemoteGameSession）
// ===================================================================

void GamePanel::onRemoteGameEvent(const GameEvent &event)
{
    // 引擎座位 s ↔ relay playerId s+1；再映射到本地 Player*（User/左/右机器人）。
    auto seatPlayer = [this](int seat) -> Player * {
        if (!isValidSeat(seat))
            return nullptr;
        return playerForRelayId(seat + 1);
    };

    switch (event.type)
    {
    case GameEventType::RoundStarted:
    {
        // 新一局：仅清空本地 UI/Player 状态，全部置为农民，等待私有手牌与叫分。
        // 不能调 resetCardData()——那会启动本地 LocalSession 发一副随机牌，与服务器
        // 权威事件冲突。联机模式下本地引擎必须保持休眠。
        m_gameControl->setCurrentPlayer(nullptr);
        m_gameControl->syncPendingInfo(nullptr, Cards());
        m_relayLordCards.clear();
        m_passStreak = 0;
        m_gameControl->getUserPlayer()->clearCards();
        m_gameControl->getLeftRobot()->clearCards();
        m_gameControl->getRightRobot()->clearCards();
        m_gameControl->getUserPlayer()->setRole(Player::Farmer);
        m_gameControl->getLeftRobot()->setRole(Player::Farmer);
        m_gameControl->getRightRobot()->setRole(Player::Farmer);
        clearPlayArea();
        clearLordCards();
        updateLordIcons();
        updateAllPlayerCards();
        m_musicPlayer->playBGM(MusicPlayer::BGM_Playing);
        m_statusLabel->setText(QStringLiteral("发牌中..."));
        m_chatPanel->addSystemMessage(QStringLiteral("🎮 新一局开始"));
        m_btnRestart->hide();
        break;
    }

    case GameEventType::PrivateHandDealt:
    {
        // 仅本座位会收到自己的完整手牌。用 view 里已应用好的本地手牌重建 UI。
        if (event.seat == m_remoteSession->localSeat())
        {
            UserPlayer *user = m_gameControl->getUserPlayer();
            user->clearCards();
            user->storeDispatchCard(event.cards);
            updateUserCards();
        }
        break;
    }

    case GameEventType::TurnChanged:
    {
        Player *p = seatPlayer(event.seat);
        if (!p)
            break;
        m_gameControl->setCurrentPlayer(p);
        applyRemoteTurnUI(p);
        break;
    }

    case GameEventType::BidAccepted:
    {
        Player *p = seatPlayer(event.seat);
        if (!p)
            break;
        const int bet = event.value;
        QString txt = (bet == 0) ? QStringLiteral("不叫")
                                  : QString::number(bet) + QStringLiteral("分");
        m_statusLabel->setText(p->getName() + ": " + txt);
        switch (bet) {
        case 0: m_musicPlayer->playSFX(MusicPlayer::SFX_NoCall); break;
        case 1: m_musicPlayer->playSFX(MusicPlayer::SFX_CallLord1); break;
        case 2: m_musicPlayer->playSFX(MusicPlayer::SFX_CallLord2); break;
        case 3: m_musicPlayer->playSFX(MusicPlayer::SFX_CallLord3); break;
        }
        m_chatPanel->addSystemMessage(QStringLiteral("🎯 %1: %2").arg(p->getName(), txt));
        break;
    }

    case GameEventType::LordSelected:
    {
        Player *lord = seatPlayer(event.seat);
        if (!lord)
            break;
        lord->setRole(Player::Lord);
        for (Player *other : {static_cast<Player *>(m_gameControl->getUserPlayer()),
                              static_cast<Player *>(m_gameControl->getLeftRobot()),
                              static_cast<Player *>(m_gameControl->getRightRobot())})
            if (other != lord)
                other->setRole(Player::Farmer);

        // 底牌公开：地主本人的手牌已由 view 加进本地手牌（PrivateHandDealt 已到）。
        m_relayLordCards = event.cards;
        if (lord == m_gameControl->getUserPlayer())
            m_gameControl->getUserPlayer()->storeDispatchCard(event.cards);

        updateAllPlayerCards();
        updateLordCards();
        updateLordIcons();
        m_statusLabel->setText(QStringLiteral("地主: %1").arg(lord->getName()));
        break;
    }

    case GameEventType::CardsPlayed:
    {
        Player *p = seatPlayer(event.seat);
        if (!p)
            break;
        m_passStreak = 0;
        if (p == m_gameControl->getUserPlayer())
        {
            // 本座位：从本地手牌真正移除（view 也已移除，二者保持一致）。
            CardList cl = event.cards.toCardList(Cards::Asc);
            for (const Card &c : cl)
                p->removeCard(c);
        }
        m_gameControl->syncPendingInfo(p, event.cards);

        PlayHand h(event.cards);
        m_statusLabel->setText(p->getName() + ": " + PlayHand::handTypeName(h.getHandType()));
        m_musicPlayer->playSFXForHandType(h.getHandType());
        m_chatPanel->addSystemMessage(
            QStringLiteral("🃏 %1: %2 (%3张)")
                .arg(p->getName(), PlayHand::handTypeName(h.getHandType()))
                .arg(event.cards.cardCount()));
        updatePlayArea(p, event.cards);
        updateAllPlayerCards();
        break;
    }

    case GameEventType::PlayerPassed:
    {
        Player *p = seatPlayer(event.seat);
        if (!p)
            break;
        m_passStreak++;
        m_statusLabel->setText(p->getName() + QStringLiteral(": 不出"));
        m_musicPlayer->playSFX(MusicPlayer::SFX_Pass1);
        m_chatPanel->addSystemMessage(QStringLiteral("🚫 %1: 不出").arg(p->getName()));
        updatePlayArea(p, Cards());
        break;
    }

    case GameEventType::TrickReset:
        // 一墩结束：清桌 + 清 pending，领出权回到 event.seat。
        clearPlayArea();
        m_gameControl->syncPendingInfo(nullptr, Cards());
        break;

    case GameEventType::MultiplierChanged:
        m_chatPanel->addSystemMessage(QStringLiteral("✖️ 倍数变为 %1").arg(event.value));
        break;

    case GameEventType::ScoreChanged:
    {
        Player *p = seatPlayer(event.seat);
        if (p)
            p->setScore(event.value);
        break;
    }

    case GameEventType::RoundFinished:
    {
        Player *winner = seatPlayer(event.seat);
        m_countdownTimer->stop();
        m_timerLabel->hide();
        if (!winner)
            break;

        bool isLordWin = (winner->getRole() == Player::Lord);
        QString r = isLordWin ? QStringLiteral("👑地主胜利!") : QStringLiteral("🌾农民胜利!");
        QString msg = QStringLiteral("%1\n%2").arg(winner->getName(), r);
        m_statusLabel->setText(msg);
        m_musicPlayer->stopBGM();

        UserPlayer *user = m_gameControl->getUserPlayer();
        if (winner == user ||
            (winner->getRole() == Player::Farmer && user->getRole() == Player::Farmer))
            m_musicPlayer->playSFX(MusicPlayer::SFX_Win);
        else
            m_musicPlayer->playSFX(MusicPlayer::SFX_Lose);

        clearPlayArea();
        m_chatPanel->addSystemMessage(QStringLiteral("🏆 ") + msg);
        // 以 this 作 context object，窗口 300ms 内销毁则自动取消，避免悬垂 this。
        QTimer::singleShot(300, this, [msg, this]() {
            QMessageBox::information(this, QStringLiteral("游戏结束"), msg);
        });

        setPlayButtonsVisible(false);
        setCallButtonsVisible(false);
        // 仅房主能重开（发 StartGameReq），其余等待房主开新局。
        m_btnRestart->setVisible(m_isHost);
        m_btnBackMenu->show();
        break;
    }

    case GameEventType::RoundVoided:
        m_statusLabel->setText(QStringLiteral("🔄 无人叫地主，重新发牌"));
        m_chatPanel->addSystemMessage(QStringLiteral("🔄 全员不叫，本局作废，重新发牌"));
        clearPlayArea();
        clearLordCards();
        break;

    case GameEventType::CardVanished:
        // Chaos mode is single-player only; not expected on the online path.
        break;
    }
}

void GamePanel::onRemoteCommandRejected(GameError error)
{
    m_statusLabel->setText(QStringLiteral("❌ 操作被服务器拒绝"));
    m_chatPanel->addSystemMessage(
        QStringLiteral("❌ 操作无效（错误码 %1），请重试").arg(static_cast<int>(error)));

    // 若仍轮到本座位，恢复操作按钮让玩家重试。
    if (m_remoteSession &&
        m_remoteSession->viewState().currentSeat == m_remoteSession->localSeat())
        applyRemoteTurnUI(m_gameControl->getUserPlayer());
}

void GamePanel::applyRemoteTurnUI(Player *current)
{
    // 依据服务器视图的阶段，决定当前该玩家显示叫分还是出牌 UI。
    setCallButtonsVisible(false);
    setPlayButtonsVisible(false);
    m_countdownTimer->stop();
    m_timerLabel->hide();

    const bool isUser = (current == m_gameControl->getUserPlayer());
    const GamePhase phase = m_remoteSession ? m_remoteSession->viewState().phase
                                            : GamePhase::Waiting;

    if (phase == GamePhase::CallingLord)
    {
        if (isUser)
        {
            setCallButtonsVisible(true);
            m_statusLabel->setText(QStringLiteral("请选择叫分"));
        }
        else
        {
            m_statusLabel->setText(current->getName() + QStringLiteral(" 思考中..."));
        }
    }
    else if (phase == GamePhase::Playing)
    {
        if (isUser)
        {
            setPlayButtonsVisible(true);
            m_statusLabel->setText(QStringLiteral("请出牌"));
        }
        else
        {
            m_statusLabel->setText(current->getName() + QStringLiteral(" 思考中..."));
        }
    }

    if (isUser && (phase == GamePhase::CallingLord || phase == GamePhase::Playing))
    {
        m_countdownSec = 25;
        m_timerLabel->setText(QString::number(m_countdownSec));
        m_timerLabel->show();
        m_countdownTimer->start();
    }
}

void GamePanel::onRelayError(const QString &error)
{
    m_chatPanel->addSystemMessage(QStringLiteral("⚠️ 网络错误: %1").arg(error));
    m_statusLabel->setText(QStringLiteral("⚠️ 网络错误"));
}

// ===================================================================
// relay playerId ↔ 本地座位映射 / 在线状态管理
// ===================================================================

int GamePanel::localRelayPlayerId() const
{
    if (m_relayClient && m_relayClient->myPlayerId() > 0)
        return m_relayClient->myPlayerId();
    return 1;
}

Player *GamePanel::playerForRelayId(int playerId) const
{
    switch (relaySeatForPlayerId(localRelayPlayerId(), playerId)) {
    case RelaySeat::User:
        return m_gameControl->getUserPlayer();
    case RelaySeat::RightOpponent:
        return m_gameControl->getRightRobot();
    case RelaySeat::LeftOpponent:
        return m_gameControl->getLeftRobot();
    case RelaySeat::Unknown:
        break;
    }
    return nullptr;
}

int GamePanel::relayIdForPlayer(Player *player) const
{
    if (!player)
        return -1;
    if (player == m_gameControl->getUserPlayer())
        return playerIdForRelaySeat(localRelayPlayerId(), RelaySeat::User);
    if (player == m_gameControl->getRightRobot())
        return playerIdForRelaySeat(localRelayPlayerId(), RelaySeat::RightOpponent);
    if (player == m_gameControl->getLeftRobot())
        return playerIdForRelaySeat(localRelayPlayerId(), RelaySeat::LeftOpponent);
    return -1;
}

QString GamePanel::relayNameForPlayerId(int playerId) const
{
    if (Player *player = playerForRelayId(playerId))
        return player->getName();
    return QStringLiteral("P%1").arg(playerId);
}

void GamePanel::ensureRemoteSession()
{
    if (m_remoteSession || !m_relayClient)
        return;
    m_remoteSession = new RemoteGameSession(m_relayClient, this);
    connect(m_remoteSession, &RemoteGameSession::eventApplied,
            this, &GamePanel::onRemoteGameEvent);
    connect(m_remoteSession, &RemoteGameSession::commandRejected,
            this, &GamePanel::onRemoteCommandRejected);
}

int GamePanel::remoteHandCountFor(Player *player) const
{
    // 联机模式下对手手牌不在本地（服务器权威只下发数量），从 view 取 handCount。
    if (!m_remoteSession || !player)
        return player ? player->getCards().cardCount() : 0;

    int seat = kInvalidSeat;
    const int localId = localRelayPlayerId();
    if (player == m_gameControl->getUserPlayer())
        seat = playerIdForRelaySeat(localId, RelaySeat::User) - 1;
    else if (player == m_gameControl->getRightRobot())
        seat = playerIdForRelaySeat(localId, RelaySeat::RightOpponent) - 1;
    else if (player == m_gameControl->getLeftRobot())
        seat = playerIdForRelaySeat(localId, RelaySeat::LeftOpponent) - 1;

    if (!isValidSeat(seat))
        return player->getCards().cardCount();
    return m_remoteSession->viewState().players[seat].handCount;
}

void GamePanel::configureOnlinePlayers()
{
    // Single-player: the user's button clicks drive LocalSession directly.
    // Online: bids/plays go over the network instead, so the engine-driving
    // connections must stay dormant to avoid a conflicting second game engine.
    m_gameControl->setUserInputDrivesEngine(!m_isOnlineMode);
}

void GamePanel::wireRelayClient()
{
    // 幂等：仅在首次进入联机（建房/加入）时创建客户端并接线，之后复用同一实例。
    // 建房与加入的接线完全相同，此前在 onCreateRoom/onJoinRoom 各抄了一份。
    if (m_relayClient)
        return;

    m_relayClient = new RelayClient(this);
    connect(m_relayClient, &RelayClient::connected,         this, &GamePanel::onRelayConnected);
    connect(m_relayClient, &RelayClient::disconnected,      this, &GamePanel::onRelayDisconnected);
    connect(m_relayClient, &RelayClient::roomCreated,       this, &GamePanel::onRoomCreated);
    connect(m_relayClient, &RelayClient::roomJoined,        this, &GamePanel::onRoomJoined);
    connect(m_relayClient, &RelayClient::roomJoinFailed,    this, &GamePanel::onRoomJoinFailed);
    connect(m_relayClient, &RelayClient::playerJoined,      this, &GamePanel::onRemotePlayerJoined);
    connect(m_relayClient, &RelayClient::playerLeft,        this, &GamePanel::onRemotePlayerLeft);
    connect(m_relayClient, &RelayClient::roomDissolved,     this, &GamePanel::onRoomDissolved);
    connect(m_relayClient, &RelayClient::gameStarted,       this, &GamePanel::onRelayGameStarted);
    connect(m_relayClient, &RelayClient::gameMessageReceived, this, &GamePanel::onRelayGameMessage);
    connect(m_relayClient, &RelayClient::errorOccurred,     this, &GamePanel::onRelayError);
}

void GamePanel::resetOnlineState()
{
    m_isOnlineMode = false;
    m_isHost = false;
    m_remotePlayers = 0;
    m_pendingRoomId = 0;
    m_relayLordCards.clear();

    // 销毁 RemoteGameSession，避免留下持有过期视图的僵尸会话（此前只有 onBackToMenu
    // 清理它，而断连/入房失败/房间解散走的是本函数）。它只持有 m_relayClient 的裸
    // 回指针、并不拥有客户端，故删它安全；注意：本函数常从 m_relayClient 自身的信号
    // 槽中调用，绝不能在此删除 m_relayClient（那由 onBackToMenu 负责）。
    if (m_remoteSession)
    {
        delete m_remoteSession;
        m_remoteSession = nullptr;
    }
    m_countdownTimer->stop();
    m_timerLabel->hide();

    configureOnlinePlayers();
    m_btnStart->hide();
    m_roomIdLabel->hide();
    m_playerListLabel->hide();
    setPlayButtonsVisible(false);
    setCallButtonsVisible(false);
}

void GamePanel::updateRoomStartVisibility()
{
    if (!m_isOnlineMode)
    {
        m_btnStart->hide();
        return;
    }
    m_btnStart->setVisible(m_isHost && m_remotePlayers >= 3);
}
