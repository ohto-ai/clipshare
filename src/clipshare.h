#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_clipshare.h"

class clipshare : public QMainWindow
{
    Q_OBJECT

public:
    clipshare(QWidget *parent = Q_NULLPTR);

private:
    Ui::clipshareClass ui;
};
