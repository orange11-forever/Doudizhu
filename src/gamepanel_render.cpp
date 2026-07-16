// ===================================================================
// gamepanel_render.cpp —— GamePanel 的卡牌渲染 / 布局编排 / 按钮显隐
//
// 从 gamepanel.cpp 拆出（原 God Class 重构第一步，见架构审查"先迁出
// CardRenderer"）。这里只承担纯表现层：创建/销毁 CardPanel、按窗口尺寸
// 排列坐标、把交互按钮 raise 到卡牌层之上。不含任何游戏流程/网络逻辑。
//
// 与 gamepanel.cpp 同属 GamePanel 类的实现（同一翻译单元集合），共用
// gamepanel.h 中的成员与 CARD_W/CARD_HEIGHT/OVERLAP 静态常量。
// ===================================================================

#include "gamepanel.h"
#include "cardpanel.h"

#include <QLabel>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>

// ===================================================================
// 卡牌渲染
// ===================================================================

void GamePanel::updateAllPlayerCards()
{
    updateUserCards();
    updateRobotCards(m_gameControl->getLeftRobot());
    updateRobotCards(m_gameControl->getRightRobot());
}

void GamePanel::updateUserCards()
{
    // 清空旧控件
    for (auto *p : m_userCardPanels)
        p->deleteLater();
    m_userCardPanels.clear();
    m_userCards.clear();
    m_selectedIds.clear();

    // 获取降序排列的用户手牌
    m_userCards = m_gameControl->getUserPlayer()->getCards().toCardList(Cards::Desc);

    // 为每张牌创建 CardPanel
    for (int i = 0; i < m_userCards.size(); ++i)
    {
        const Card &c = m_userCards[i];
        CardPanel *p = new CardPanel(m_cardLayer);

        // 加载正面图片并缩放
        QPixmap f = CardPanel::loadCardPixmap(c)
                        .scaled(CARD_W, CARD_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p->setFrontPixmap(f);
        p->setBackPixmap(m_cardBack);
        p->setFrontSide(true);             // 用户手牌默认正面朝上
        p->setCard(c);
        // 牌背用主题梗图（正面保持正常扑克牌）。主题是单机本地装饰，联机各端不同步，
        // 故联机模式一律用默认牌背，避免房主/加入方看到不同牌背。
        if (!m_isOnlineMode && m_themeManager->isLoaded())
        {
            QPixmap themeBack = m_themeManager->cardBack();
            if (!themeBack.isNull())
                p->setThemeBack(themeBack);
        }
        p->setOwner(m_gameControl->getUserPlayer());
        p->show();

        // 点击信号连接
        int idx = i;
        connect(p, &CardPanel::clicked, this, [this, idx] { onCardSelected(idx); });

        m_userCardPanels.append(p);
    }
    arrangeUserCards();
    raiseGameButtons(); // 确保按钮不被新卡牌遮挡
}

void GamePanel::updateRobotCards(Player *robot)
{
    // 选择左/右机器人对应的控件容器
    QVector<CardPanel *> *v = (robot->getDirection() == Player::Left)
                                  ? &m_leftRobotCards
                                  : &m_rightRobotCards;

    // 清空旧控件
    for (auto *p : *v)
        p->deleteLater();
    v->clear();

    // 最多显示 6 张背面牌（美观 + 性能考虑）。联机模式对手牌不在本地，用服务器视图的数量。
    int handCount = m_isOnlineMode ? remoteHandCountFor(robot) : robot->getCards().cardCount();
    int n = qMin(handCount, 6);
    for (int i = 0; i < n; ++i)
    {
        CardPanel *p = new CardPanel(m_cardLayer);
        p->setFrontPixmap(m_cardBack);
        // 先设默认牌背兜底，再用主题牌背覆盖。顺序不能反：setThemeBack 和
        // setBackPixmap 都写同一个 m_back，若默认牌背放在主题之后会把主题覆盖掉，
        // 导致任何主题都只显示默认牌背（机器人牌是背面朝上，正是玩家看到的那面）。
        p->setBackPixmap(m_cardBack);
        // 联机模式主题不同步（见 updateUserCards 注释），一律用默认牌背。
        if (!m_isOnlineMode && m_themeManager->isLoaded())
        {
            QPixmap tb = m_themeManager->cardBack();
            if (!tb.isNull())
                p->setThemeBack(tb);
        }
        p->setFrontSide(false); // 背面朝上
        p->show();
        v->append(p);
    }
    arrangeRobotCards(robot);
}

void GamePanel::updatePlayArea(Player *player, const Cards &cards)
{
    clearPlayArea();

    if (cards.isEmpty())
    {
        // 显示出"不出"标签
        m_playLabel->setText(QStringLiteral("不出"));
        m_playLabel->show();
        return;
    }

    m_playLabel->hide();

    // 为每张牌创建 CardPanel
    CardList l = cards.toCardList(Cards::Desc);
    for (const Card &c : l)
    {
        CardPanel *p = new CardPanel(m_cardLayer);
        QPixmap f = CardPanel::loadCardPixmap(c)
                        .scaled(CARD_W, CARD_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p->setFrontPixmap(f);
        p->setBackPixmap(m_cardBack);
        p->setFrontSide(true);
        p->show();
        m_playAreaPanels.append(p);
    }

    // 机器人出牌 → 添加缩放+淡入动画（用户出牌无需动画，直接排列）
    Player *user = m_gameControl ? m_gameControl->getUserPlayer() : nullptr;
    if (player && player != user)
    {
        // 计算居中位置
        int tw = m_playAreaPanels.size() * (CARD_W + 8) - 8;
        QRect bgGeo = m_bgLabel ? m_bgLabel->geometry() : QRect(0, 0, width(), height());
        int cx = bgGeo.x() + bgGeo.width() / 2;
        int cy = bgGeo.y() + bgGeo.height() / 2;
        int sx = qMax(10, cx - tw / 2);
        int targetY = qMax(10, cy - CARD_HEIGHT / 2 - 20);

        for (int i = 0; i < m_playAreaPanels.size(); ++i)
        {
            CardPanel *p = m_playAreaPanels[i];

            // 目标位置
            QRect finalRect(sx + i * (CARD_W + 8), targetY, CARD_W, CARD_HEIGHT);

            // 起始状态：缩小 + 居中
            int startW = CARD_W / 2;
            int startH = CARD_HEIGHT / 2;
            int startX = cx - startW / 2;
            int startY = cy - startH / 2 - 20;
            QRect startRect(startX, startY, startW, startH);
            p->setGeometry(startRect);
            p->show();
            p->raise();

            // 透明度效果（从透明渐入）
            QGraphicsOpacityEffect *effect = new QGraphicsOpacityEffect(p);
            effect->setOpacity(0.0);
            p->setGraphicsEffect(effect);

            // 几何动画：缩放+移动
            QPropertyAnimation *animGeom = new QPropertyAnimation(p, "geometry");
            animGeom->setDuration(360);
            animGeom->setStartValue(startRect);
            animGeom->setEndValue(finalRect);
            animGeom->setEasingCurve(QEasingCurve::OutCubic);

            // 透明度动画：淡入
            QPropertyAnimation *animOpacity = new QPropertyAnimation(effect, "opacity");
            animOpacity->setDuration(280);
            animOpacity->setStartValue(0.0);
            animOpacity->setEndValue(1.0);

            // 并行播放
            QParallelAnimationGroup *group = new QParallelAnimationGroup(this);
            group->addAnimation(animGeom);
            group->addAnimation(animOpacity);
            group->start(QAbstractAnimation::DeleteWhenStopped); // 动画结束后自动清理
        }
    }
    else
    {
        // 用户出牌：直接定位排列
        arrangePlayArea();
    }
    raiseGameButtons(); // 确保按钮不被出牌区卡牌遮挡
}

void GamePanel::updateLordCards()
{
    clearLordCards();

    // 联机（服务器权威）下底牌来自 LordSelected 事件缓存的 m_relayLordCards；
    // 单机走引擎的 surplus。房主与加入方在权威模式下同样不跑本地引擎。
    Cards lc = (m_isOnlineMode && !m_relayLordCards.isEmpty())
        ? m_relayLordCards
        : m_gameControl->getSurplusCards();
    if (lc.isEmpty())
        return;

    CardList l = lc.toCardList(Cards::Desc);
    for (const Card &c : l)
    {
        CardPanel *p = new CardPanel(m_cardLayer);
        QPixmap f = CardPanel::loadCardPixmap(c)
                        .scaled(CARD_W, CARD_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p->setFrontPixmap(f);
        p->setBackPixmap(m_cardBack);
        p->setFrontSide(true); // 底牌正面朝上
        p->show();
        m_lordCardPanels.append(p);
    }
    arrangeLordArea();
}

void GamePanel::updateLordIcons()
{
    // 显示/隐藏地主图标
    m_userLordIcon->setVisible(
        m_gameControl->getUserPlayer()->getRole() == Player::Lord);
    m_userLordIcon->setText(QStringLiteral("👑地主"));

    m_leftLordIcon->setVisible(
        m_gameControl->getLeftRobot()->getRole() == Player::Lord);
    m_leftLordIcon->setText(QStringLiteral("👑地主"));

    m_rightLordIcon->setVisible(
        m_gameControl->getRightRobot()->getRole() == Player::Lord);
    m_rightLordIcon->setText(QStringLiteral("👑地主"));
}

void GamePanel::clearPlayArea()
{
    for (auto *p : m_playAreaPanels)
        p->deleteLater();
    m_playAreaPanels.clear();
    m_playLabel->hide();
}

void GamePanel::clearLordCards()
{
    for (auto *p : m_lordCardPanels)
        p->deleteLater();
    m_lordCardPanels.clear();
}

// ===================================================================
// 布局编排
// ===================================================================

void GamePanel::arrangeUserCards()
{
    if (m_userCardPanels.isEmpty())
        return;

    // 居中排列手牌，每张牌间隔 OVERLAP=26px
    int tw = m_userCardPanels.size() * OVERLAP + CARD_W - OVERLAP;
    int sx = qMax(5, (width() - tw) / 2);     // 水平居中
    int y = height() - CARD_HEIGHT - 80;        // 底部固定位置

    for (int i = 0; i < m_userCardPanels.size(); ++i)
    {
        CardPanel *p = m_userCardPanels[i];
        // 选中的牌向上偏移 15px
        p->move(sx + i * OVERLAP, m_selectedIds.contains(i) ? y - 15 : y);
        p->raise();
    }
}

void GamePanel::arrangeRobotCards(Player *robot)
{
    QVector<CardPanel *> *v;
    int bx; // 基准 X 坐标

    if (robot->getDirection() == Player::Left)
    {
        v = &m_leftRobotCards;
        bx = 20;
    }
    else
    {
        v = &m_rightRobotCards;
        bx = width() - 215;
    }

    int handCount = m_isOnlineMode ? remoteHandCountFor(robot) : robot->getCards().cardCount();
    int n = qMin(handCount, 6);
    for (int i = 0; i < n && i < v->size(); ++i)
    {
        // y=72：避开上方信息面板（头像 y=8~60、昵称/分数/地主图标 y=8~48），
        // 防止牌堆顶边压住标签、透明留边把标签裁成细线。
        (*v)[i]->move(bx + i * 10, 72); // 每张牌向右偏移 10px 堆叠
        (*v)[i]->raise();
        (*v)[i]->show();
    }
    // 隐藏多余的旧控件
    for (int i = n; i < v->size(); ++i)
        (*v)[i]->hide();
}

void GamePanel::arrangePlayArea()
{
    // 出牌区居中排列
    int tw = m_playAreaPanels.size() * (CARD_W + 8) - 8;
    int sx = qMax(10, (width() - tw) / 2);
    int y = height() / 2 + 30;
    for (int i = 0; i < m_playAreaPanels.size(); ++i)
    {
        m_playAreaPanels[i]->move(sx + i * (CARD_W + 8), y);
        m_playAreaPanels[i]->raise();
    }
}

void GamePanel::arrangeLordArea()
{
    // 底牌区居中排列（位于出牌区上方）
    int lx = width() / 2 - 130;
    int ly = height() / 2 - CARD_HEIGHT - 30;
    for (int i = 0; i < m_lordCardPanels.size(); ++i)
        m_lordCardPanels[i]->move(lx + i * (CARD_W + 6), ly);
}

// ===================================================================
// 按钮显隐控制
// ===================================================================

void GamePanel::raiseGameButtons()
{
    // 将所有交互按钮提升到卡片层的最上层，防止被新创建的卡牌遮挡
    m_btnVsAI->raise();       m_btnCreateRoom->raise();    m_btnJoinRoom->raise();
    m_btnStart->raise();      m_btnRestart->raise();       m_btnBackMenu->raise();
    m_btnPlay->raise();       m_btnPass->raise();          m_btnHint->raise();
    m_btnCall1->raise();      m_btnCall2->raise();         m_btnCall3->raise();
    m_btnNoCall->raise();
    if (m_roomIdLabel) m_roomIdLabel->raise();
    if (m_playerListLabel) m_playerListLabel->raise();
    if (m_btnChaosMode) m_btnChaosMode->raise();
    if (m_btnAvatar) m_btnAvatar->raise();
    if (m_themeCombo) m_themeCombo->raise();
    if (m_btnConfirmTheme) m_btnConfirmTheme->raise();
    // 确保文字标签不被卡牌遮挡
    if (m_statusLabel) m_statusLabel->raise();
    if (m_playLabel) m_playLabel->raise();
    if (m_timerLabel) m_timerLabel->raise();
    // 三家信息面板（头像/昵称/分数/地主图标）提到卡牌层之上，避免牌堆顶边压住标签
    // 被裁成细线（此前这几个标签从未 raise，卡牌层盖在其上导致右上角"细红/金线"瑕疵）。
    for (QLabel *lbl : {m_leftAvatar, m_leftNameLabel, m_leftLordIcon,
                        m_rightAvatar, m_rightNameLabel, m_rightLordIcon,
                        m_userAvatar, m_userNameLabel, m_userLordIcon})
        if (lbl) lbl->raise();
    // 横幅最后 raise，保持在所有控件之下（仅视觉展示）
    if (m_titleLogo) m_titleLogo->raise();
    if (m_chaosBanner) m_chaosBanner->raise();

}

void GamePanel::setCallButtonsVisible(bool v)
{
    m_btnCall1->setVisible(v);
    m_btnCall2->setVisible(v);
    m_btnCall3->setVisible(v);
    m_btnNoCall->setVisible(v);
    if (v) raiseGameButtons();
}

void GamePanel::setPlayButtonsVisible(bool v)
{
    m_btnPlay->setVisible(v);
    m_btnPass->setVisible(v);
    m_btnHint->setVisible(v);
    if (v) raiseGameButtons();
}

void GamePanel::setMenuButtonsVisible(bool v)
{
    m_btnVsAI->setVisible(v);
    m_btnCreateRoom->setVisible(v);
    m_btnJoinRoom->setVisible(v);
}
