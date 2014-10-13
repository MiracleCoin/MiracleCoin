#ifndef MARKETPLACEPAGE_H
#define MARKETPLACEPAGE_H

#include <QWidget>

namespace Ui {
class MarketplacePage;
}

class MarketplacePage : public QWidget
{
  Q_OBJECT
  
public:
  explicit MarketplacePage(QWidget *parent = 0);
  ~MarketplacePage();
  
private:
  Ui::MarketplacePage *ui;
};

#endif // MARKETPLACEPAGE_H
