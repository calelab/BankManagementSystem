# 深蓝金主题修改前备份

original-source.zip 保存了修改前的全部 Git 跟踪文件，包含当时磁盘上的实际内容。manifest.json 记录原提交和文件校验值。

如需撤销本次界面修改，可从压缩包恢复本次修改的原文件，再移除新增的主题资源文件。恢复前应先检查是否又产生了后续修改，避免覆盖。此备份不包含运行时银行数据，也没有修改 Git 暂存区或历史。

## 本次修改范围

需要恢复的原文件：

- CMakeLists.txt
- mainwindow.cpp
- mainwindow.h
- ui/depositdialog.cpp
- ui/passworddialog.cpp
- ui/profiledialog.cpp
- ui/unfreezedialog.cpp
- ui/withdrawdialog.cpp

新增主题文件：

- ui/theme.h
- ui/theme.cpp
- assets/navy-gold.qss
- assets/chevron-down.svg
- assets/chevron-up.svg
- assets/check.svg

所有原始 .ui 文件及业务、存储代码均未改动。build/theme-review/ 中是临时演示数据生成的界面预览与检查文件，可在不需要时移除。
