#ifndef BOTSPAGE_H
#define BOTSPAGE_H

#include <QWidget>

class OptionsModel;
class SellBot;

namespace Ui {
class BotsPage;
}

class BotsPage : public QWidget
{
  Q_OBJECT
  
public:
  explicit BotsPage(QWidget *parent = 0);
  ~BotsPage();

  void setOptionsModel(OptionsModel* model);

private:
  Ui::BotsPage *ui;
  SellBot* _sellBot;
};

#endif // BOTSPAGE_H
