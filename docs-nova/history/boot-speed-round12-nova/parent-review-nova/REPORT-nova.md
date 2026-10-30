# R12 主代理增量复核 — Nova

新文件，无旧版。冻结源码经实际SHA校验；生产diff只有新增日志及mkdir返回值的局部副本，逐块移除新增文本后恢复R11原SHA。目录/文件谓词、首错errno和cold流程未变。printf各项类型显式转换与格式相符；目录行在CHECK_DIRECTORY后、原predicate前，保存原mkdir errno未覆盖error。

复用已通过的747实际函数ASan/UBSan和同R11 recipe目标编译，不重复无新依据的大规模回归。当前已存0:20目录依旧拒绝，未把候选当修复或提速完成。

按用户最新要求，仅允许本地构建和封包；未安装，等待明确安装命令。
