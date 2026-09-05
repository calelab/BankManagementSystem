# 银行储蓄管理系统

这是一个使用 C++17、Qt Widgets 和 CMake 实现的课程设计项目。系统以储户账户为聚合边界，支持多笔独立定期存款、存取款计息、挂失、查询、未来三日备款和按营业员审计。OpenSSL 可用时，核心业务数据与审计日志默认使用 AES-256-GCM 加密保存。

## 环境要求

- CMake 3.19 或更高版本；
- Qt 6.5 或更高版本，当前验收环境为 Qt 6.11.2；
- 支持 C++17 的编译器；
- OpenSSL 3（正式加密模式需要，无 OpenSSL 时可使用独立明文兼容模式）。

macOS 使用 Homebrew 时可安装 OpenSSL：

```bash
brew install openssl@3
```

## 默认加密构建

将 `QT_ROOT` 替换为本机 Qt 安装目录，例如 Qt 安装器中的 `6.11.2/macos`：

```bash
export QT_ROOT=/path/to/Qt/6.11.2/macos
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DBANK_ENABLE_ENCRYPTION=ON \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

配置输出应包含 `AES-256-GCM enabled`。若找不到 OpenSSL，CMake 会给出明确警告并编译明文兼容模式；该回退不会使用或覆盖加密文件。

macOS 可直接运行：

```bash
./build/BankManagementSystem.app/Contents/MacOS/BankManagementSystem
```

程序启动日志会显示当前是 `AES-256-GCM 加密模式` 还是 `明文 JSON 兼容模式`。首次运行会生成营业员工号 `E01` 至 `E10`，选择任一有效工号即可进入工作台。

## 无 OpenSSL 兼容构建

兼容构建显式关闭加密，使用独立构建目录和 `bank_data.json`、`*.audit.json` 文件：

```bash
cmake -S . -B build-plain \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DBANK_ENABLE_ENCRYPTION=OFF
cmake --build build-plain --parallel
ctest --test-dir build-plain --output-on-failure
```

加密模式使用 `bank_data.enc` 和 `*.audit.enc`，两种模式的文件不会互相覆盖。兼容模式只用于缺少 OpenSSL 的构建环境和开发排错，不代表数据已加密。

## 严格警告构建

可使用单独目录把常见编译警告提升为错误：

```bash
cmake -S . -B build-strict \
  -DCMAKE_PREFIX_PATH="$QT_ROOT" \
  -DBANK_ENABLE_ENCRYPTION=ON \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
cmake --build build-strict --parallel
ctest --test-dir build-strict --output-on-failure
```

## 数据与主密钥

正式运行数据位于 Qt `QStandardPaths::AppDataLocation` 返回的平台应用数据目录，而不是源码目录。逻辑结构如下：

```text
BankManagementSystemData/
├── bank_data.enc
├── employees.dat
├── config/master.key
└── audit/E01.audit.enc
```

启用加密时，`config/master.key` 是随机生成的 32 字节主密钥。`master.key`、`bank_data.enc` 和所有 `audit/*.audit.enc` 必须作为一个整体备份和迁移；缺少原主密钥时，系统会拒绝读取既有密文，也不会生成替代密钥覆盖数据。`employees.dat` 不依赖主密钥，但整目录备份更不容易遗漏文件。

主密钥管理属于课程展示级本地方案，不等同于生产银行系统的密钥托管。不要提交、分享或输出真实主密钥、业务数据、审计日志及真实密码。

## 项目结构

- `models/`：储户、定期存款、交易、审计记录和银行状态聚合；
- `services/`：核心业务编排与统一计息；
- `persistence/`：严格 JSON、编码边界和原子文件写入；
- `security/`：PBKDF2、随机数、AES-256-GCM 和主密钥；
- `audit/`：按营业员隔离的审计日志；
- `ui/`、`mainwindow.*`：Designer 界面、对话框和业务连接；
- `tests/`：领域、持久化、审计、服务和 UI 自动测试。

详细业务规则、文件格式、安全边界及验收资料见 [课程设计说明](docs/course-design-summary.md) 和 [最终人工验收清单](docs/manual-acceptance-checklist.md)。
