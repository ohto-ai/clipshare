#pragma once

#include <QtWidgets/QMainWindow>
#include <QSystemTrayIcon>
#include "ui_clipshare.h"

class QClipboard;

class clipshare : public QMainWindow
{
    Q_OBJECT

public:
    clipshare(QWidget *parent = Q_NULLPTR);
    QSystemTrayIcon systemTrayIcon{ this };

private:
    Ui::clipshareClass ui;
};
