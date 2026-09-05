# 银行储蓄管理系统课程设计说明

## 一、总体设计

系统采用分层结构：Qt Widgets 页面负责输入和展示，`BankService` 统一执行业务规则，领域模型维护对象自身约束，持久化层负责 JSON 与原子文件写入，安全层封装密码派生和 AES-256-GCM，审计层按营业员记录业务事件。UI 不直接修改本金或磁盘文件。

主要类关系：

```text
BankState
└── 多个 Depositor
    ├── 多个 FixedDeposit
    └── 多个 Transaction（通过 depositId 关联具体存款）

MainWindow → BankService → BankState
                         ├── InterestCalculator
                         ├── FileManager → BankStateJsonSerializer → DataCodec
                         └── AuditLogger → DataCodec
```

一个储户可以持有多笔相互独立的 `FixedDeposit`。每次新增存款都会生成新的存款编号和交易编号；支取必须明确选中某一笔存款，系统不会跨存款自动凑款。

## 二、核心业务规则

### 定期存款与本金

- 支持一年、三年、五年期定期存款，年利率分别为 1.98%、2.25%、3.50%；
- 金额统一使用 `qint64` 整数分，利率统一使用整数基点，避免浮点误差；
- `originalPrincipal` 保存存入时的原始本金，之后保持不变；
- `remainingPrincipal` 保存当前尚未支取的本金，部分支取后递减，归零后该笔存款结清；
- 利率在存入时锁定，到期日按存入日增加完整日历年计算。

### 支取与计息

- 到期日前支取属于提前支取，按本次支取本金、实际持有天数和 0.05% 年利率计算；
- 到期日当天及以后属于正常到期支取，按本次支取本金、存入时固定年利率和完整期限计算；
- 不自动续存、不复利，也不对到期后的额外天数继续计息；
- 每笔利息使用统一整数规则四舍五入到分，实际支付额为本次本金加本次利息；
- 预览支取不会修改账户，只有用户确认后才创建交易并递减剩余本金。

计算形式：

```text
提前支取利息 = 四舍五入到分(本金分 × 5基点 × 实际天数 ÷ 10000 ÷ 365)
到期支取利息 = 四舍五入到分(本金分 × 固定年利率基点 × 期限年数 ÷ 10000)
```

### 挂失、会话和登录限制

- 营业员先以 `E01` 至 `E10` 中的有效工号进入工作台；
- 储户完整明细和敏感业务还需要账号与密码登录；
- 挂失账户仍可登录查看，但禁止存款、支取、修改资料和修改密码；
- 解除挂失必须再次验证当前密码；
- 同一账号连续五次登录失败后，在本次程序运行期锁定 60 秒；成功登录或锁定期结束后清除相应计数；
- 不存在账号和错误密码使用相同提示，减少账号枚举风险。

### 未来三日备款

- 统计范围固定为基准日期后的明天、后天和大后天，不包含今天和第 4 天；
- 结果始终返回三天，某天没有到期存款时显示零；
- 只统计尚有 `remainingPrincipal` 的存款；
- 每笔到期利息复用 `InterestCalculator`，先单笔舍入到分，再使用 `qint64` 汇总当天本金、利息和预计备款。

## 三、持久化与文件格式

`BankStateJsonSerializer` 把完整对象图转换为带 `schemaVersion` 的 UTF-8 JSON。账号、编号、密码派生参数、挂失信息、多笔存款和全部交易都会保存。金额及编号序列以十进制字符串保存，避免 JSON 数值无法无损表达全部 64 位整数。

反序列化先读取到局部候选对象，并逐层执行领域约束校验；只有全部成功才替换调用者状态。缺字段、类型错误、未知 Schema、重复编号、错误利率、矛盾到期日和无效对象关联都会导致加载失败。

核心数据与审计日志均通过 `QSaveFile` 写临时文件后原子替换。项目关闭直接覆盖回退，因此序列化、编码、写入或提交任一步失败时都会保留上一版完整文件。

### AES-256-GCM 封装

OpenSSL 可用时，明文 JSON 整体使用 AES-256-GCM 加密。文件布局为：

```text
固定头（20 字节）
  Magic             8 字节：BMSAESG1
  格式版本          1 字节
  Nonce 长度        1 字节
  Tag 长度          1 字节
  保留位            1 字节
  密文长度          8 字节，大端序
Nonce              12 字节
Authentication Tag 16 字节
Ciphertext          变长
```

固定头作为 AAD 参与认证。Nonce 每次加密随机生成，Authentication Tag 同时保护文件头和密文；格式字段异常、Tag 不匹配、密钥错误或密文被篡改都会拒绝加载，认证成功前不会向 JSON 层发布任何候选明文。

加密模式使用 `bank_data.enc` 与 `*.audit.enc`；无 OpenSSL 兼容模式使用 `bank_data.json` 与 `*.audit.json`。文件名隔离保证两种模式互不覆盖。

## 四、密码与审计安全

储户密码不以明文保存。系统为每次开户或改密生成独立 16 字节 Salt，使用 PBKDF2-HMAC-SHA256、210000 次迭代派生 32 字节结果，并使用恒定时间比较验证密码。改密会同时更换 Salt 和派生结果。

AES 主密钥首次运行时安全随机生成并保存到 `config/master.key`，在类 Unix 系统上尽力限制为当前用户读写。只在既无密钥也无任何加密数据时允许创建；若已有核心密文或加密审计却缺少密钥，系统会明确失败，绝不生成替代密钥。

`AuditLogger` 按营业员保存独立文件，记录日期时间、工号、账号、稳定动作码、本金、利息、结果和原因码，不记录密码、Salt、哈希或主密钥。由于 GCM 密文不能安全地原地尾追加，当前课程规模采用读取完整日志、追加记录、重新编码并原子替换。

## 五、功能对照

| 验收功能 | 实现位置 | 自动验证 |
|---|---|---|
| 开户、账号密码登录 | `BankService`、开户/登录页面 | `BankServiceTests`、`BankUiTests` |
| 修改资料、修改密码 | `BankService`、资料/密码对话框 | `BankServiceTests`、`BankUiTests` |
| 挂失、密码验证解挂 | `BankService`、账户中心 | `BankServiceTests`、`BankUiTests` |
| 1/3/5 年定期存款 | `FixedDeposit`、存款对话框 | `BankDomainTests`、`BankServiceTests` |
| 多笔独立定期存款 | `Depositor::deposits` | `BankServiceTests`、`BankUiTests` |
| 提前/正常到期支取 | `InterestCalculator`、支取对话框 | `BankDomainTests`、`BankServiceTests` |
| 全部储户及组合查询 | `queryDepositors`、查询页面 | `BankServiceTests`、`BankUiTests` |
| 指定储户全部业务 | `currentAccountDetails`、账户中心 | `BankServiceTests`、`BankUiTests` |
| 文件持久化和重启恢复 | `FileManager` | `BankPersistenceTests`、`BankServiceTests`、`BankUiTests` |
| 本金与利息计算 | `InterestCalculator` | `BankDomainTests`、`BankServiceTests` |
| 未来三日到期备款 | `reserveForecast`、备款页面 | `BankServiceTests`、`BankUiTests` |
| 密码安全与登录限制 | `SecurityUtils`、`BankService` | `BankServiceTests` |
| 核心数据和审计加密 | `AesGcmCodec`、`AuditLogger` | `BankPersistenceTests`、`BankAuditTests` |
| 异常与原子失败保护 | 各层结果对象、候选状态、`QSaveFile` | 全部非 UI 测试套件 |

## 六、测试与截图材料

项目注册五个 CTest 套件：领域、持久化、审计、业务服务和 UI。测试使用固定日期、可注入时钟和临时数据目录，不接触用户真实应用数据。

2026-09-04 最终自动验证结果：

- 默认 AES-256-GCM Debug 构建：找到 OpenSSL，5/5 个 CTest 套件通过；
- 严格警告加密构建：启用 `-Wall -Wextra -Wpedantic -Werror`，编译通过且 5/5 个 CTest 套件通过；
- 显式关闭 OpenSSL 的兼容构建：编译通过且 5/5 个 CTest 套件通过；
- 五个 Qt Test 可执行文件合计 93 个测试通过，失败 0、跳过 0。

课程报告建议保留以下实际运行截图，截图前使用专门的演示账户并避免显示真实密码或敏感数据：

1. 营业员入口与工作台；
2. 开户成功及储户登录；
3. 同一账户显示多笔定期存款和交易流水；
4. 提前支取预览中的本金、利息和实际支付额；
5. 挂失后的禁用按钮与解除挂失；
6. 全部储户及查询结果；
7. 明天、后天、大后天三行备款结果；
8. 当前营业员中文审计日志；
9. 重启后恢复的数据；
10. 应用数据目录中存在加密文件的证明，只显示文件名，不展示主密钥内容。
