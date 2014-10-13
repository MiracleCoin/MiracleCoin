#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <QWidget>
#include <QNetworkAccessManager>

#include <set>

class WalletModel;
class QTimer;
class QStringList;
class QTreeWidgetItem;

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
  void _requestFinished(QNetworkReply* reply);
  void _refreshData();
  void notificationsEnabledChanged(bool);
  void on_listMarkets_itemClicked(QTreeWidgetItem *item, int column);
private:
  void _sendRequest(const QString& url);
  void _resheduleDataRefresh();
  void _parseReply(const std::string& reply);
  void _parseError(const char* const what);
  void _notifyUpdateFound();
private:
  Ui::Notifications *ui;
  WalletModel* _walletModel;
  QNetworkAccessManager _netManager;
  QTimer* _refreshDataTimer;
  typedef std::set<std::string> KnownMarketList;
  KnownMarketList _knownMarketList;
  bool _firstUpdate;
};

#endif // NOTIFICATIONS_H
