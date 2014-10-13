#ifndef BOTSPAGE_H
#define BOTSPAGE_H

#include <QWidget>

namespace Ui {
class BotsPage;
}

class BotsPage : public QWidget
{
  Q_OBJECT
  
public:
  explicit BotsPage(QWidget *parent = 0);
  ~BotsPage();
  
private:
  Ui::BotsPage *ui;
};

#endif // BOTSPAGE_H
