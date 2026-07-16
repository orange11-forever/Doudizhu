# 斗地主 DouDiZhu

一个基于 **Qt6 / C++17** 的斗地主（Fight the Landlord）卡牌游戏，支持**单机人机对战**与**服务器权威（server-authoritative）联机对战**。

- 🎮 单机：一名玩家 + 两名 AI，AI 出牌/叫分策略基于贪心算法
- 🌐 联机：独立 Relay 中转服务器统一裁决牌局，客户端只发意图、只渲染视图（防作弊）
- 🀄 完整斗地主规则：叫地主、16 种牌型识别与比较、春天/反春天、倍数计算
- 🎨 主题皮肤、头像、聊天表情、背景音乐/音效，以及一个"耄耋（Chaos）"娱乐模式
- ✅ 引擎层 + 服务器端到端的自动化回归测试（CTest）

---

## 架构亮点

核心逻辑收敛在 `game_core/`，是一套**纯确定性、无副作用、不依赖 UI** 的引擎：

- **命令 → 事件（Command/Event）模型**：输入 `GameCommand`（意图），输出 `QVector<GameEvent>`（既成事实）。命令要么整体接受、要么整体拒绝且状态不变，天然事务性，且事件流可回放、可日志。
- **一套引擎/事件模型，三端复用**：
  | 消费者 | 是否内含引擎 | 角色 |
  |---|---|---|
  | `LocalSession` | 是 | 单机本地裁决 |
  | `ServerRoomGame`（relay 侧） | 是 | 服务器权威裁决 |
  | `RemoteGameSession`（客户端） | **否** | 纯视图端，只消费服务器事件更新只读视图 |
- **信息投影防作弊**：`projectedStateFor(seat)` 只保留本座位手牌，其他座位仅暴露牌数；服务器端 `PrivateHandDealt` 是唯一私有事件（点对点下发），其余全公开广播。座位由服务器会话推导、忽略客户端载荷；洗牌种子用 CSPRNG。
- **共享 AI 策略**：单机与"掉线 AI 托管"复用同一份 `AIPolicy`，内建兜底保证不卡死。

---

## ⚠️ 项目现状与已知技术债

引擎/网络/服务器层（`game_core/`、`relay/`）结构清晰、有测试覆盖，是项目质量较高的部分。

**技术债集中在 UI 层的 `GamePanel`（主窗口），目前是一个典型的 God Class：**

- 已按职责把实现**拆分到多个翻译单元**：
  ```
  gamepanel.cpp        750 行   // 初始化/构造骨架（尚未拆完）
  gamepanel_net.cpp    650 行   // 网络 / Relay 联机层
  gamepanel_game.cpp   571 行   // 单机游戏流程 / 回合回调 / 用户操作
  gamepanel_menu.cpp   467 行   // 菜单 / 模式切换 / 主题 / 头像
  gamepanel_render.cpp 395 行   // 卡牌渲染 / 布局 / 按钮显隐
  gamepanel.h          266 行   // 共享的类声明
  ```
- **但"拆分"不等于"解耦"**：上述 `.cpp` 仍是**同一个 `GamePanel` 类**，方法全部声明在同一个头文件里，共享约 60 个私有成员（`m_gameControl` / `m_relayClient` / `m_remoteSession` / 各类卡牌控件 / 主题混沌头像状态……）。渲染、网络、菜单代码摸的是同一堆可变状态，模块间没有边界与归属。
- 头文件仍 `#include` 全套依赖，任一依赖变动会触发 5 个 `.cpp` 全量重编。

**后续改进方向**：抽出真正的协作者（如 `BoardRenderer`、`RelaySessionController`、`MenuController`），各自持有私有状态、只暴露窄接口、以信号通信，让 `GamePanel` 退化为薄协调层。当前的多 TU 拆分是这一步的前置准备。

> 说明：`docs/` 内部设计/审查笔记未包含在公开仓库中。

---

## 目录结构

```
game_core/            纯逻辑引擎：GameEngine（命令→事件）、GameState、序列化、AIPolicy、LocalSession
relay/                独立 Relay 中转服务器：RelayServer / Room / ServerRoomGame + main.cpp
gamepanel*.{h,cpp}    主窗口 GUI（见上文技术债）
gamecontrol_adapter.* 桥接 GamePanel ↔ LocalSession（保留旧信号接口）
relay_client.*        联机客户端（连接 Relay 服务器）
game_core/remote_game_session.*  客户端服务器权威视图会话
card/cards/playhand/player/strategy/robot/userplayer   基础牌型、玩家、AI 策略
chatpanel / musicplayer / theme_manager / chaos_engine / sticker_store / avatar_store  辅助功能
networkdata.h         网络协议定义（消息类型 + 卡牌编解码）
tests/                引擎层单元测试 + 服务器端到端回归测试
resources.qrc         资源（图片 / 音频 / 主题）
```

---

## 构建

### 依赖

- **CMake ≥ 3.16**、支持 **C++17** 的编译器（MSVC / GCC / Clang）
- **客户端**：Qt **6.8+**，模块 `Core` `Widgets` `Network` `Multimedia`（测试另需 `Test`）
- **Relay 服务器**：Qt **6.2+**，仅需 `Core` `Network`（放宽版本便于在自带 Qt6 的 Linux 服务器上编译）

> 下面的命令用 `-DCMAKE_PREFIX_PATH` 指向你的 Qt6 安装（例如 `C:/Qt/6.8.0/msvc2022_64` 或 `~/Qt/6.8.0/gcc_64`）。

### 1. 构建客户端（游戏本体）

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
```

联机模式下客户端要连的 Relay 服务器地址在**编译期注入**（默认 `127.0.0.1`）：

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/Qt6 \
      -DDOUDIZHU_RELAY_HOST=YOUR.RELAY.SERVER.IP
cmake --build build
```

运行时也可用环境变量 `DOUDIZHU_RELAY_HOST` 覆盖该默认值。

### 2. 构建并运行 Relay 服务器（联机才需要）

`relay/` 是一个独立的控制台程序，有自己的 CMake 工程：

```bash
cmake -S relay -B build-relay -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build-relay

# 启动（默认端口 9527）
./build-relay/DouDiZhuRelay --port 9527
```

### 3. 运行测试

```bash
cmake -B build -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6
cmake --build build
ctest --test-dir build --output-on-failure
```

测试覆盖：牌型识别、发牌、引擎回合流程、序列化（含截断包）、单机会话、客户端视图会话，以及用真实 TCP socket 跑的服务器权威端到端回归（私有发牌 / 越权出牌拒绝 / 掉线 AI 托管 / 限流 / 满员与不存在房间拒绝 / 聊天冒充拒绝等）。

---

## 联机怎么玩

1. 在一台可达的机器上编译并运行 **Relay 服务器**（开放对应端口，默认 9527）。
2. 客户端编译时用 `-DDOUDIZHU_RELAY_HOST=<服务器IP>`（或运行时设 `DOUDIZHU_RELAY_HOST` 环境变量）。
3. 一人"创建房间"拿到 6 位房间号，另两人"加入房间"，房主开始游戏。

> 当前 Relay **无身份认证/房间口令**，仅适合熟人小范围联机；对外正式服务需自行补充认证、TLS、频率限制等（服务器已内置单 IP 并发连接限流作为基础防护）。

---

## 资源说明

图片与音频资源来自网络（部分文件名带来源站水印），仅用于学习交流；如需再分发请自行确认各资源的授权。
