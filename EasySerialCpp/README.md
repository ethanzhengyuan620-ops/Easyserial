# EasySerial

这是原 PyQt6 版本的独立 C++/Qt 6 重写版，源代码与 Python 版本互不覆盖。

## 技术结构

- Qt 6 Widgets：主界面和工具箱。
- Qt 6 SerialPort：事件驱动串口收发，不使用高频轮询线程。
- 接收区使用只读 `QTextEdit` 原生文本视图，支持像素级平滑滚动、字符级拖选、双击选词和 `Ctrl+C`，不解析设备文本中的 HTML。发送区仍使用 `QPlainTextEdit`。
- 后台模型保留有界原始帧历史；文档最多保留 5000 个逻辑文本块，超限淘汰旧数据。常规接收增量追加，窗口缩放无需重建 ASCII 文档。
- ASCII 和纯 HEX 随宽度原生自动折行，软折行不向复制内容添加换行符；HEX 双栏按宽度计算字节数并同步滚动，保留原始帧 HEX 复制。
- ASCII 同方向数据按连续文本追加，不再因 20 ms 空闲分块或 4096 字节安全分块额外换行。保留原始换行和空行，跨块 CRLF 只换一次行；周期时间戳延后至下一处接收块之间的真实行边界，避免截断未结束的行。方向变化和 NOTE 仍单独分组，HEX 仍按帧展示。
- 新配置默认 9 pt。Ctrl+滚轮的各显示模式/发送区字号自动保存，重启恢复；A± 统一调整并保存全部数据窗格字号，修改配色不会重置字号。
- 接收帧 5 ms 合并通知、视图 30 ms 批量更新，100 ms 刷新收发计数。
- 自动跟随和鼠标滚轮上下滚动固定使用平滑动画，不再提供 `Smooth scroll` 开关；旧配置中关闭动画的选项不再生效。动画按约 16 ms 节拍移动，连续输入更新同一目标，不排队播放。自动跟随时，大于两屏的突发积压直接追上，不延迟接收时间戳或日志。
- `Auto scroll` 是唯一的跟随开关：开启时每批新数据都继续滚到最新位置，滚轮、选字或搜索不会自动暂停它；查看历史需手动取消勾选。没有恢复跟随的悬浮按钮。滚轮支持系统滚动行数、像素滚动、高分辨率增量和快速反向；Ctrl+滚轮仍调字号，HEX 双栏同步。
- 常规历史裁剪只移除旧帧前缀；关闭 Auto scroll 时保留阅读锚点及未淘汰选区。单次超大数据或超大边界帧仍使用有界尾部裁剪重建，避免整帧丢弃大量仍可保留的历史。
- 发送使用 256 KiB / 256 请求的有界队列，TX 只按 `bytesWritten` 确认的字节计数，并监控驱动写入停滞。
- 自动回复使用独立分帧器，可选“按换行”或“按帧间隔”，混合 ASCII/HEX 规则仍严格按列表顺序匹配。
- 独立日志线程使用 16 MiB 有界队列；文本控制字符会转义，非法 UTF-8 会按无损 HEX 记录。
- JSON 配置，存放在 Qt 的 `AppConfigLocation/EasySerialCpp/config.json`。

保留的功能包括串口连接、ASCII/HEX 发送、HEX 模式不追加 CR/LF、三种显示模式、按接收间隔归组与正文自动折行、定时发送、快捷指令、自动回复、配色规则、搜索、日志录制、窗口置顶和外观设置。

工具箱保留：

- ASCII ↔ HEX
- Modbus CRC16（仅 CRC 计算工具）
- SHA256
- 时间戳转换
- 自动回复配置
- PB Decoder（可导入多个 `.proto`，附带 `TaskBatch.proto` 示例）

没有 Modbus Poll、Modbus Slave 或 RTU 工作台。

## PB 报文解析

工具箱 `PB Decoder` 左侧输入 HEX，右侧输出可复制的缩进 JSON；默认消息类型 `TaskBatchResDTO`，输入停止 250 ms 后自动解析，也可关闭 `Auto parse` 手动点击 `Parse`。

- 默认从字节标记 `00 00 FF` 后开始。不存在标记会报错，多处标记必须选择 `After 00 00 FF (offset)` 并指定从 0 开始的标记字节偏移；已有纯 PB 数据可选 `Raw PB (no header)`。
- 点击 `Import .proto…` 可多选协议文件；`Protocol` 切换文件，`Message` 选择带包名的消息类型（含嵌套类型和依赖中的消息）。每个入口文件独立加载，同名消息的不同协议版本不会互相覆盖。`TaskBatch.proto` 只是开箱即用的示例，解析不再受它限制。
- 支持 proto2/proto3 的标量、字符串、枚举、嵌套消息、packed/unpacked repeated、map、oneof、optional/required 和扩展字段。使用官方 Protobuf 34.2 C++ 导入器与动态消息，不执行代码生成命令，不需要用户安装 `protoc`。
- `import` 自动查找已导入文件的目录。跨文件夹项目用 `Include folders…` 添加 import 根目录，例如 `import "common/types.proto"` 应添加 `common` 的父目录；最多 16 个自定义根目录，按列表顺序查找。常用 `google/protobuf/*.proto` 标准协议随 EXE 内置。语法错误显示文件名、行和列；缺少依赖明确提示，不显示旧 JSON。
- 协议列表、include 目录和每个文件选择的消息类型自动保存（不保存报文）。修改协议后点 `Reload`；移动/删除文件后需要重新导入。`Remove` 只移除列表条目，不删除磁盘文件。一次只加载当前文件及其依赖，最多保留 128 个协议条目。
- 默认补齐普通标量默认值和重复字段空数组，可取消 `Include defaults`；proto2 显式默认值也会保留，缺失 required 字段产生警告。字段名使用 `json_name` 或 lowerCamelCase，所有 64 位整数用十进制字符串防止精度损失，枚举用数字，NaN/Infinity 用字符串。**bytes 用小写 HEX 展示，不是标准 ProtoJSON 的 Base64**；标准消息按其字段展示，不进行 Timestamp/Any 等 ProtoJSON 特殊转换。参考 [Protobuf 编码](https://protobuf.dev/programming-guides/encoding/) 和 [ProtoJSON](https://protobuf.dev/programming-guides/json/)。
- `Skipped prefix` 单独保留被跳过的全部包头（含标记），不会将截图中的 `extData` 猜作 PB 字段；该字段不在所提供的 proto 中。`TaskResMO.data` 按 bytes 保留，不猜测其中的其他业务协议，也不会访问报文内的 URL。
- 未知字段记录在 `_unknownFields`，包含编号、wire type 和重新编码的 HEX；匹配不到字段时提醒检查消息类型。错误不输出部分解析结果，修改输入立即清除旧结果，避免将旧 JSON 当作当前结果。PB 数据本身没有消息类型标识，选错消息类型仍可能解析成功，请核对协议。
- 本地解析，输入文本上限 1,048,576 字符、解码数据上限 256 KiB，输出最多 8192 个字段/条目、32 层嵌套；每次协议加载限制 128 个依赖文件、单文件 2 MiB、总计 8 MiB、4096 个消息类型。导入路径不能含 `..`，只读取指定根目录内的 `.proto` 文件。仅影响工具箱，不改变串口收发或显示分帧。

首次构建会下载固定版本、SHA256 校验的 Protobuf 34.2 和 Abseil 20250512.1 源码，之后可离线构建；可预先将对应归档放在 `build/downloads`。运行库静态链接到程序，不增加需要单独安装的组件。官方许可证和标准协议一起嵌入 Qt resources；仅首次编译依赖耗时较长。

## 窗口监控模式

点击接收区右上角的 `Monitor Mode`，切换成默认位于屏幕右下角的置顶浮窗。主界面暂时隐藏，复用同一个接收视图、串口连接和数据历史；日志、自动回复、定时发送继续工作。

- 拖动标题区域移动，松手后平滑吸附到当前屏幕最近的角落；窗口边缘可调整大小。
- 标题栏内置三个 28 px 图标：电源图标按主界面串口配置连接/断开（绿色表示已连接）；向下箭头切换 `Auto scroll`（蓝色表示开启，与主界面及配置同步）；垃圾桶清除显示历史和计数，不删除录制文件、不清除发送草稿。连接失败或未选择端口时，原因直接显示在浮窗状态及悬停提示里，可继续重试。
- 四角图标 `Position` 可指定四角，也可选 `Free position` 关闭自动吸附。定位避开任务栏，并在显示器移除或工作区变化时重新限制到可见区域。所有按钮保留在原标题栏中，默认 480×320、最小 360×200 尺寸不变，不增加工具栏行。
- `Opacity` 调节背景不透明度（30%–100%），文字和按钮保持不透明。
- `Font size` 调节监控字号（6–36 pt），与 Ctrl+滚轮同步；监控字号独立保存，不改变正常界面和发送区字号。
- `Restore`、右上角 × 或 Alt+F4 返回正常界面，不断开串口；恢复原窗口尺寸、位置、最大化状态、置顶状态和分隔条布局。
- 自动保存浮窗尺寸、位置、吸附选项、背景不透明度和字号；下次启动仍先显示正常界面。

## 无编译环境时的静态检查

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\static_check.ps1
```

该脚本会检查 UTF-8、本地头文件、CMake 源文件清单和已知回归模式，但不能代替真实的 Qt/C++ 编译。

## Windows 构建环境

本机已安装 Qt 6.11.2 / MinGW 13.1，推荐在本目录运行：

```powershell
.\build.ps1
```

脚本会编译、运行回归测试，并将独立发布包生成在 `EasySerialCpp\dist\EasySerial.exe`。
程序名称为 EasySerial，界面使用英文，窗口和 EXE 使用同一套多分辨率信号芯片图标。
发布目录只保留单文件版 `EasySerial.exe`，可以单独复制、改名和运行，无需安装 Qt 或复制 DLL。
这是免安装自解压启动包，不是静态链接：使用 [Windows Cabinet API](https://learn.microsoft.com/en-us/windows/win32/api/fdi/nf-fdi-fdicopy) 在每次启动时解压到独立临时目录，等待程序退出后清理自身解压的文件。启动会增加少量解压耗时；强制终止启动器或断电可能留下临时目录。
用户配置仍保存在原配置路径，退出清理不会删除配置或用户日志。未使用管理员权限、不创建安装项；未对 EXE 做代码签名，因此 Windows 可能显示未知发布者提示。
构建中间依赖位于 `build/runtime`，不作为第二个发行版本保留；原 Python 版根目录 `dist` 不受影响。
`build.ps1` 会自动打包；已有已测试构建只需重新封装时可运行 `./package-single.ps1 -SkipBuild`。
默认浅色主题；已有自定义颜色会保留。详细核查结果和尚存差异见 [PARITY.md](PARITY.md)。

### 其他可选工具链

建议安装：

- Visual Studio 2022，并勾选“使用 C++ 的桌面开发”
- Qt 6.5 或更高版本，组件选择 MSVC 2022 64-bit 和 Qt Serial Port
- CMake 3.28 或更高版本
- Ninja（可选）

示例：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix dist
```

使用 Visual Studio 多配置生成器时：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

`cmake --install` 会调用 Qt 的部署脚本，把运行所需 Qt DLL 和插件放入发布目录。若将来需要真正的单文件静态链接版本，需要单独准备静态 Qt；普通动态部署启动更快，也更符合 Qt LGPL 的常见分发方式。

## 源码目录

```text
src/
  core/   串口、分帧、自动回复、配色、日志线程和 HEX 工具
  model/  虚拟化接收数据模型
  ui/     主窗口、接收视图、发送区和工具对话框
```
