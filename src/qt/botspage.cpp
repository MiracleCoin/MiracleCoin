#include "botspage.h"
#include "ui_botspage.h"

BotsPage::BotsPage(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::BotsPage)
{
  ui->setupUi(this);
}

BotsPage::~BotsPage()
{
  delete ui;
}
