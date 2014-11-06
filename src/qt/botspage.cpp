#include "botspage.h"
#include "ui_botspage.h"

#include "sellbot.h"

#include <QVBoxLayout>

BotsPage::BotsPage(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::BotsPage),
  _sellBot(0)
{
  ui->setupUi(this);
  QVBoxLayout* layout = new QVBoxLayout(this);
  _sellBot = new SellBot(this);
  layout->addWidget(_sellBot);
}

BotsPage::~BotsPage()
{
  delete ui;
}


void BotsPage::setOptionsModel(OptionsModel* model)
{
  Q_ASSERT(_sellBot);
  _sellBot->setOptionsModel(model);
}
