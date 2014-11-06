#ifndef SELLBOT_H
#define SELLBOT_H

#include <QWidget>

namespace Ui {
class SellBot;
}

class OptionsModel;

namespace sellbot {
class SellBotPrivate;
}

class SellBot : public QWidget
{
  Q_OBJECT

public:
  explicit SellBot(QWidget *parent = 0);
  ~SellBot();
  void setOptionsModel(OptionsModel* model);
private slots:
    void updateControls();
    void on_buttonTrade_clicked();
    void on_listMarkets_currentIndexChanged(const QString &arg1);
    void botsBittrexEnabledChanged(const bool enabled);
private:
  Ui::SellBot *ui;
  sellbot::SellBotPrivate* _private;
  OptionsModel* _optionsModel;

  friend class sellbot::SellBotPrivate;
};

#endif // SELLBOT_H

