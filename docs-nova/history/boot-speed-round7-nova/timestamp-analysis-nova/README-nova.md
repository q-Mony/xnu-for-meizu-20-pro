# R7 时间戳日志离线解析器

新文件，无旧版本。此目录拥有独立工具、测试和结果，不改旧时间线工具、生产源码或设备。仅使用 Python 标准库。

## 运行

工作目录为 `<nova-root>`。输出必须是本工具目录内尚不存在的新目录。

```sh
PYTHONDONTWRITEBYTECODE=1 python3 artifacts-nova/boot-speed-round7-nova/timestamp-analysis-nova/analyze-timestamps-nova.py \
  --log logs-nova/boot-speed-round7-nova/candidate-run-nova/autolog-nova.txt \
  --output artifacts-nova/boot-speed-round7-nova/timestamp-analysis-nova/round7-first-analysis-nova
```

上面的输入现在已有 R7 真机采集；输出示例若已存在，必须换为新的目录名。可用 `--top-gaps 30` 调整榜单长度。默认同时调用 SHA256 冻结的旧时间线解析器；`--skip-legacy` 可只生成新报告。旧解析器变化时会拒绝静默复用。

## 输出和证据链

| 文件 | 含义 |
| --- | --- |
| `autolog-timestamped-nova.txt` | 输入的逐字节副本，保留原始前缀和损坏片段 |
| `autolog-unprefixed-nova.txt` | 只移除完整、结构匹配的 v1 前缀，正文和换行不重编码 |
| `source-map-nova.json` | 原始/派生文件 SHA256；0 起点、半开区间的字节映射；行号从 1 开始，按 LF 计算 |
| `timestamp-analysis-nova.json` | 全部前缀原值、合法性、分段、gap、阶段配对、捕获点、autologio、clockcal及异常 |
| `REPORT-nova.md` | 中文摘要、gap榜、显式阶段表、日志写盘成本表 |
| `legacy-timeline-nova.json` / `legacy-REPORT-nova.md` | 旧工具读取派生正文得到的结果；附原件 hash 和映射，不把新前缀时间合入旧计量 |
| `provenance-nova.json` | 原始路径/hash、工具/hash、旧工具/hash和所有输出/hash |

输入文件在分析前后核对字节不变，输出拒绝覆盖已有目录或符号链接。原件和派生件都是新的文件，不重写采集日志。

## 前缀和 gap 的边界

固定格式为 `[nova-ts:v1 c=cntvct t=0123456789abcdef f=01234567 k=P] `：t 为 16 位小写十六进制 CNTVCT，f 为 8 位小写十六进制 CNTFRQ，闭括号后恰好一个协议空格。k 只允许 P、L、R。

P 是 printbuf invocation 的首字符，L 是同 invocation 内换行后的首字符，R 是手工裸消息开始。前缀可在物理行中间，所以按 bytes 扫描，不能只匹配行首。一行多个前缀、裸行、inline数量都保留清单。不沿用最近前缀给裸行补时间。

v1 没有文本转义或调用ID；解析器将**精确匹配的保留协议串**解释为仪表前缀。若正文故意包含同一完整保留串，单凭此文本格式无法区分，必须回到生产输出语义审查。前缀原子预留并不使整个消息或 ring snapshot 原子化。

未知版本、错误kind/clock、坏hex和截断前缀保留原字节并形成断点。零hz的结构完整前缀会移除，但时间不参与计算。计数倒退和频率变化断段；不排序掩盖。末尾无换行的一条记录保持不完整，其中标签不作为gap端点。

榜单仅是“相邻有效timestamp标签间隔”。两个标签间仍可能持续输出正文或交织其他writer；它既不证明完全没有日志字节，也不是前一行函数耗时。中间额外无标签行单列说明。0 tick间隔单独计数，不表示实际同时执行。

## 阶段、成本与时钟

明确阶段仅支持 `root_device_resolve`、`root_mount`、`systemos_prepare`、`data_prepare`。接受完整的 `nova: bootphase v=1 phase=... id=... edge=begin/end clock=mach_absolute ticks=... ns=... result=...`，按同一唯一id和phase配对，以内部Mach ns相减。重复id/边界、错phase、缺端点、倒序、交错嵌套或跨损坏记录都保持未知。正常嵌套可独立计算，失败返回码保留为已结束但未成功的尝试。

阶段记录边界直接核对原始 bytes：允许完整的无前缀物理行，或由有效 `P` 前缀精确界定开头、以 LF/CRLF 结束的完整记录。因此前一个普通消息缺少换行时，同一行后面的合法 `P` 阶段记录仍能识别。不得从任意正文 substring 猜测起点；零频率、错误 kind、标签后额外引用文字、内部/尾部交织标签、多个阶段挤在同一物理行、坏前缀或不完整尾行均拒绝。JSON 保留实际 payload 字节范围及 framing 前缀索引。

阶段值是包围这两个探针的经过时间，包含begin记录输出成本；不是纯CPU或纯函数执行时间。CNTVCT前缀不参与Mach阶段相减。每次root_mount重试独立id，不合并。

exec-map/setup-arm的Mach capture点仅是成功exec后的捕获点，不是第一条用户指令、服务ready或Hello；Setup只有 `status=armed` 且非零捕获值参与，cap-reached保持未知。

autologio表逐次显示累计copy/write/attempt时长、单次最长write、attempts/writes/ok。copy和write包含在attempt等重叠范围内，不能相加。计数/时间倒退、clock_errors或跨损坏记录不生成对应时长增量。

clockcal仅输出该次 Vbefore→Mach→Vafter 的bracket宽度和CNTVCT减Mach的offset范围，允许范围为负，不自动精确映射所有事件，也不映射根盘CNTPCT。

Boot点击、苹果首次可见、Hello首次可见始终独立保留为未知；没有从第一/最后标签、Setup或framebuffer推算，也不以设备计数器值作为北京时间。

## 已做验证

```sh
PYTHONDONTWRITEBYTECODE=1 python3 artifacts-nova/boot-speed-round7-nova/timestamp-analysis-nova/test_timestamp_analysis_nova.py
```

29 项测试通过；当前实际输出和哈希在 `tests-inline-20261029T193518790113Z-nova/`。覆盖fragment、多行/CRLF/无效字节、0hz、坏hex/kind、回退/换频、末尾torn、跨损坏拒桥接、真实R6无timestamp、合成前缀字节往返、阶段身份/嵌套/错误返回、Setup cap、成本重叠、校准范围及CLI拒覆盖。新增真实 R7 内联阶段回归，以及伪造 substring、无有效 P 边界、截断和内部标签交织负例。真实回归修复前的失败记录在 `tests-inline-red-20261029T193357066999Z-nova/`；早期 26 项结果保留为历史记录。

`replay-r6-no-timestamps-nova/` 是真实旧R6回放，新增有效时间戳0、gap0、显式阶段0，所有新增阶段未知。`synthetic-protocol-example-nova/` 仅为 `fixtures-nova/synthetic-protocol-nova.txt` 的合成协议示范，**不是设备实测，也不是第七轮提速证据**。这两个目录使用初版冻结工具，保留其原 provenance。真实 R7 原件 SHA256 为 `01e6b73d77676d6436c3d898dc9671ce464bcb422d6d8ffb92ed0b735147420a`；其 L102 内联 `root_device_resolve` 开始记录到 L137 结束记录为 7.385559479 秒。初版 `round7-first-analysis-nova/` 将这条内联开始记录误判，保留作历史；修订输出由主代理生成到新目录。这是阶段测量，不能视为 Boot→Hello 或提速证据。

代码修改前的备份分别位于 `snapshot-nova/round7-parser-metadata-20261029T191059342156Z-nova`、`snapshot-nova/round7-parser-final-boundaries-20261029T191337054374Z-nova` 和本次修订的 `snapshot-nova/round7-parser-inline-phase-20261029T193326016291Z-nova`。旧工具保持原 SHA256，仅按冻结版本读取。
