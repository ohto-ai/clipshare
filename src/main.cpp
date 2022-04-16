#include "clipshare.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    clipshare w;
    w.show();
    return a.exec();
}
