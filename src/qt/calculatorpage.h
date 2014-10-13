#ifndef CALCULATORPAGE_H
#define CALCULATORPAGE_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QString>
#include <QSet>
#include <QMap>

#include <string>

namespace Ui {
class CalculatorPage;
}

class WalletModel;
class CalculatorPagePrivate;

class CalculatorPage : public QWidget
{
  Q_OBJECT
  
public:
  explicit CalculatorPage(QWidget *parent = 0);
  ~CalculatorPage();
  void setModel(WalletModel *model);
  
private Q_SLOTS:
  void _requestFinished(QNetworkReply* reply);
  void _refreshData();
  void on_actionUpdateResult_triggered();
private:
  void _sendRequest(const QString& url);
  void _resheduleDataRefresh();
  void _parseMarketSummaries(const std::string& str);
  void _parseBitstamp(const std::string& str);
  void _bitcoinPriceUsdUpdated();
  void _marketsUpdated();
private:
  Ui::CalculatorPage *ui;
  WalletModel* walletModel;
  QNetworkAccessManager _netManager;
  QTimer* _refreshDataTimer;
  QSet<QString> _requestsPending;
  CalculatorPagePrivate* _private;
};

#endif // CALCULATORPAGE_H
