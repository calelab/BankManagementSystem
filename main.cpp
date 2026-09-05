// 应用入口：初始化 Qt 事件循环并启动银行管理主窗口。
#include "mainwindow.h"

#include "persistence/datacodec.h"

#include <QApplication>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qInfo().noquote() << "文件存储模式："
                      << bank::persistence::defaultStorageModeDisplayName();
    MainWindow w;
    w.show();
    return QApplication::exec();
}
