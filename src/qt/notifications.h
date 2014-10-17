#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QSet>
#include <QString>

#include <set>

class WalletModel;
class QTimer;
class QStringList;
class QTreeWidgetItem;
class NotificationsPrivate;

namespace Ui {
class Notifications;
}

class Notifications : public QWidget
{
  Q_OBJECT
  
public:
  explicit Notifications(QWidget *parent = 0);
  ~Notifications();
  void setModel(WalletModel *model);
private Q_SLOTS:
  void _resheduleDataRefresh();
  void _requestFinished(QNetworkReply* reply);
  void _refreshData();
  void notificationsEnabledChanged(bool);
  void on_listMarkets_itemClicked(QTreeWidgetItem *item, int column);
  void _newMarketFound(const QDateTime& date, const QString& name, const QString& url);
  void _notifyUpdateFound();
private:
  NotificationsPrivate* _private;
  Ui::Notifications *ui;
  QTimer* _refreshDataTimer;
  WalletModel* _walletModel;

  friend class NotificationsPrivate;
};

#endif // NOTIFICATIONS_H
