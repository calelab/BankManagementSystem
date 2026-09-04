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
