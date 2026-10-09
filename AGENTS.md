# CardCast 项目协作约定

项目对外名称统一使用 **CardCast**，设备首页使用大写 **CARDCAST**；M5Cardputer 是硬件名称，保留依赖和硬件标识。

本文件适用于整个仓库。开始修改前先阅读相关实现和 `README.md`，保留工作区中已有的用户改动，只修改当前任务涉及的内容。

## 项目结构

- 项目基于 PlatformIO、Arduino 和 M5Cardputer，设备横屏分辨率为 **240 × 135**。
- `src/main.cpp` 协调设备启动；`CardDisplay` 负责首页、串流页和启动错误显示；`CardWifiSetup` 负责扫描、网络选择及配置页面；`CardInput` 负责键盘交互。
- `WifiSettings` 和 `WifiConnection` 负责配置保存、校验与连接；`AudioServer` 负责音频采集和 HTTP/WebSocket 服务。
- `web/` 为浏览器播放器，使用原生 ES modules，没有 npm 依赖或打包步骤。
- `test/` 存放主机侧 C++ 测试和 Node.js 测试；`docs/screenshots/` 保存页面截图。

## 实现约定

- 保持现有模块职责，头文件提供声明，实现在 `.cpp` 中；不要为局部需求引入无关重构。
- 开机在 `v1.0` 首页自动扫描 Wi-Fi，有结果后进入 `Select Network`；无网络或扫描失败时在首页底部提示，等待 2 秒后重试。除非任务明确要求，不恢复独立扫描页或首页按键等待。
- 跨页面键盘交互保留按键释放保护，避免上一页的 Enter 键误提交下一页。
- 修改网络配置时保留已有配置兼容性；修改音频链路时关注缓冲上限、资源释放和失败恢复。
- 新增浏览器运行时资源时，同时更新 `platformio.ini` 的 `board_build.embed_txtfiles` 和 `src/WebAssets.cpp` 的资源注册。网页资源更新后，需要重编译并烧录才能在设备上生效。
- 用户可见行为或操作方式变化时，同步更新 `README.md`。

## 页面截图要求（必须执行）

- **新增页面或修改现有页面时，必须同步新增或更新对应截图。** 范围包括设备屏幕和浏览器页面，以及文案、版本号、布局、颜色、提示状态和页面切换流程的变化。
- 截图必须对应本次修改后的实际代码。每类页面只保留 1–2 张具有代表性的截图，不为相同布局的文案、配置值或相同状态的语言/视口组合重复存图。正常、空结果、错误和重试等受影响状态仍须验证；流程变化另行验证跳转行为。
- 将截图保存到 `docs/screenshots/`，使用明确的页面与状态名称，例如 `home.png`、`home-no-networks.png`、`home-scan-failed.png`。已有同名状态截图应更新，避免保留含义不明的旧副本。
- 设备截图保留 **240 × 135 PNG 原图**。如需放大预览，使用最近邻整数倍缩放，另存带 `-4x` 等标识的文件，不替代原图。
- 设备无法直接截图时，可使用当前页面绘制代码和项目实际使用的 M5GFX 图形库离屏渲染。复用生产绘制逻辑和字体，不用手工重画或 AI 生成图片代替验证截图。
- 浏览器截图应运行真实 `web/` 页面；允许使用模拟音频或测试数据，但必须说明。响应式布局变化至少检查移动端和桌面端；语言相关变化覆盖受影响语言。
- 查看生成的图片，确认文字完整、布局无越界、内容无遮挡、颜色正确。截图只能验证可见结果，不能代替跳转、按键、网络或音频行为验证。
- 在交付说明中链接截图，注明来源是实机、离屏渲染还是浏览器模拟，以及尚未完成的实机验证。提供生成命令或可复用脚本；新增渲染辅助代码时放入仓库，避免仅依赖 `/tmp` 中的一次性脚本。
- 如果环境限制导致无法生成截图，应明确说明缺少哪些截图及具体原因，不将页面验证报告为全部完成。

## 统一截图工具与流程

以下命令均在仓库根目录执行。默认复用这些工具和参数，不为同一页面另建手工绘制的截图实现。

### 设备页面：M5GFX 离屏渲染

固定入口为 `tools/render_home.py`，依赖 Linux 主机上的 C++17 编译器（`c++`）、Python 3、Pillow，以及 PlatformIO 安装的 M5GFX 库：

```sh
pio run
python3 tools/render_home.py
```

- 脚本直接编译 `src/CardDisplay.cpp` 和 `.pio/libdeps/m5stack-stamps3/M5GFX/src` 中的图形库，以 `LGFX_Sprite` 替代物理显示器，执行生产代码的绘制函数。
- 使用 16 位 RGB565 缓冲区，调用生产初始化设置横屏方向，通过 `readPixelRGB()` 导出 RGB888，再用 Pillow 保存 PNG。不要将 `readPixel()` 返回的 RGB565 数据直接当作 RGB888，否则颜色会失真。
- 保留原字体、字号、坐标、背景和颜色，不增加抗锯齿、阴影、设备外框、水印或装饰。离屏渲染忽略背光亮度，不模拟拍照效果。
- 当前保留 14 张代表性设备截图，清单由 `tools/render_home.py` 的 `RETAINED_STATES` 管理，页面索引见 README。所有状态仍离屏渲染和检查，只保存清单中的图片到 `docs/screenshots/`，尺寸为 240 × 135。
- 首页错误提示从生产源码读取，串流页固定使用示例 IP `192.168.1.42`。新增状态应扩展此脚本并复用实际绘制逻辑；涉及 SSID 等动态数据时使用固定、非敏感的示例值，并注明为模拟数据。
- 脚本目前自动检查原图尺寸、首页提示宽度及是否覆盖首页主体；运行后仍需查看图片，检查所有受影响页面。
- 如需放大，用 Pillow 的 `Image.Resampling.NEAREST` 放大 4 倍至 960 × 540，命名为 `<原文件名>-4x.png`。原尺寸图始终作为基准。

### 浏览器页面：Chrome 冒烟测试截图

固定入口为 `test/browser_smoke_test.js`，使用 Node.js 22+ 启动真实网页及模拟 PCM WebSocket 服务，由无头 Chrome 通过 DevTools Protocol 的 `Page.captureScreenshot` 导出 PNG。依赖已安装的 `google-chrome`，可用 `CHROME_BIN` 指定可执行文件，无需安装 npm 依赖。

```sh
SCREENSHOT_DIR=docs/screenshots/web node test/browser_smoke_test.js
AUTOPLAY=allow SCREENSHOT_DIR=docs/screenshots/web node test/browser_smoke_test.js
```

- 网页截图按终端分类：移动端统一保存到 `docs/screenshots/web/mobile/`，PC 端统一保存到 `docs/screenshots/web/pc/`。`SCREENSHOT_DIR` 指定共同根目录 `docs/screenshots/web`，脚本根据截图清单自动选择子目录，不再按自动播放场景分目录。
- 两种自动播放场景均执行完整验证；只保留 5 张代表性浏览器截图。受限场景仅保存移动端播放入口，允许场景保存两端的折叠视图和详情，避免重复。截图使用模拟音频，不连接真实设备。
- 基准截图视口为移动端 **390 × 844 CSS 像素**、桌面端 **1440 × 900 CSS 像素**，`deviceScaleFactor=2`；导出图片像素尺寸不能与 CSS 视口尺寸混淆。测试另外检查 360 × 800、768 × 1024 和 844 × 390 的布局。
- `mobile/` 保留 `play-mobile.png`、`spectrum-390.png`、`advanced-zh-390.png`；`pc/` 保留 `spectrum-1440.png`、`advanced-en-1440.png`。其他语言、视口、停止、重连和焦点状态仍执行交互与布局断言，不单独存图。新增代表性截图时，在脚本的 `retainedShots` 映射中明确指定 `mobile` 或 `pc`。
- 详情截图使用 `captureBeyondViewport: true` 保留完整内容，允许图片高度超过视口；不要裁掉标题、控制按钮或统计项以统一高度。
- 保留真实 CSS、字体、语言切换和播放状态，不另加截图专用主题，不后期修改波形或数值。动态波形和统计值可能随采样时刻变化，不要求逐像素一致。
- 新增页面或状态时扩展现有测试中的导航和截图步骤，沿用上述视口、倍率、格式及命名规则；视觉检查与交互断言都通过后再交付。

## 验证与交付

- 固件代码变化执行 `pio run`；根据修改范围运行 `README.md` 中对应的主机侧测试。
- 浏览器逻辑变化执行：

  ```sh
  node --test test/player_test.js test/language_test.js test/visualizer_test.js
  ```

- 浏览器页面或交互变化按上方统一流程执行两种自动播放场景的冒烟测试，并保留截图。

- 为行为变化选择有意义的验证，不为纯文档修改或简单文案替换增加形式化测试。纯文档修改检查内容和 `git diff --check` 即可；页面文案修改仍必须更新截图。
- 编译和模拟测试不能证明真实 Wi-Fi、麦克风、按键或端到端音频时延符合预期；交付时区分已完成验证和待实机验证。
- 总结实际改动、验证结果和必要限制。提交、推送或烧录设备按用户授权执行。
