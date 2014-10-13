#include "marketplacepage.h"
#include "ui_marketplacepage.h"

MarketplacePage::MarketplacePage(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::MarketplacePage)
{
  ui->setupUi(this);
}

MarketplacePage::~MarketplacePage()
{
  delete ui;
}
