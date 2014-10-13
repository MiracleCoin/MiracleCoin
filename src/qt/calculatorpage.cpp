#include "calculatorpage.h"
#include "ui_calculatorpage.h"

#include <QTimer>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>

#include <boost/assert.hpp>
#include <boost/lexical_cast.hpp>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include <stdexcept>
#include <cmath>

using namespace json_spirit;


static const QString URL_MARKET_SUMMARIES =
    "https://bittrex.com/api/v1.1/public/getmarketsummaries";
static const QString URL_BITSTAMP_TICKER =
    "https://www.bitstamp.net/api/ticker/";
static const QString BTC_TEXT = "BTC";
static const QString BTC_PREFIX = BTC_TEXT + "-";
static const QString USD_TEXT = "USD";
static const float PRICE_INITIAL = 0.0f;


struct MarketEntry
{
  MarketEntry() : value(PRICE_INITIAL) {}
  MarketEntry(const float avalue) : value(avalue){}
  float value;
};

class CalculatorPage;

class CalculatorPagePrivate
{
public:
  typedef std::map<QString, MarketEntry> MarketList;
private:
  CalculatorPagePrivate(CalculatorPage* calculatorPage) :
  _calculatorPage(calculatorPage),
  _bitcoinPriceUsd(PRICE_INITIAL)

{}
private:
  CalculatorPage* _calculatorPage;
  float _bitcoinPriceUsd;
  MarketList _marketList;

  friend class CalculatorPage;
};

void parseError(const char* const what)
{
  throw std::logic_error(what);
}

CalculatorPage::CalculatorPage(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::CalculatorPage),
  walletModel(0),
  _refreshDataTimer(0),
  _private(new CalculatorPagePrivate(this))
{
  ui->setupUi(this);

  ui->labelLastUpdate->setVisible(false);

  BOOST_VERIFY(connect(&_netManager, SIGNAL(finished(QNetworkReply*)),
    this, SLOT(_requestFinished(QNetworkReply*)))
    );

  _refreshDataTimer = new QTimer(this);
  BOOST_VERIFY(
    connect(_refreshDataTimer, SIGNAL(timeout()),
    this, SLOT(_refreshData())));
  _refreshDataTimer->setSingleShot(true);
}

CalculatorPage::~CalculatorPage()
{
  delete _private;
  delete ui;
}

void CalculatorPage::setModel(WalletModel *model)
{
  walletModel = model;
  if (!walletModel)
  {
    return;
  }
  _refreshData();
}


void CalculatorPage::_requestFinished(QNetworkReply* reply)
{
  const QString url = reply->url().toString();
  _requestsPending.remove(url);
  if (_requestsPending.empty())
  {
    _resheduleDataRefresh();
  }
  try
  {
    if (reply->error() != QNetworkReply::NoError)
    {
      parseError(reply->errorString().toLocal8Bit().constData());
    }
    const QByteArray arr(reply->readAll());
    const std::string str (std::string(arr.data(), arr.size()));
    if (url == URL_BITSTAMP_TICKER)
    {
      _parseBitstamp(str);
    }
    else
    {
      _parseMarketSummaries(str);
    }
  }
  catch (const std::exception& e)
  {
    ui->labelLastUpdate->setStyleSheet("color: red");
    ui->labelLastUpdate->setText(QString("Error loading data: %1").arg(e.what()));
    ui->labelLastUpdate->setVisible(true);
    return;
  }
  ui->labelLastUpdate->setVisible(false);
  //ui->labelLastUpdate->setStyleSheet("");
  //ui->labelLastUpdate->setText(QString("Last updated: %1").
  //    arg(QDateTime::currentDateTime().toString()));
}

void CalculatorPage::_refreshData()
{
  _sendRequest(URL_BITSTAMP_TICKER);
  _sendRequest(URL_MARKET_SUMMARIES);
}

void CalculatorPage::_sendRequest(const QString& url)
{
  QNetworkRequest request((QUrl(url)));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
    "application/json; charset=utf-8");
  _requestsPending.insert(url);
  _netManager.get(request);
}

void CalculatorPage::_resheduleDataRefresh()
{
  _refreshDataTimer->start(10 * 1000);
}

float parseFloat(const Value& value)
{
  switch (value.type())
  {
  case str_type:
  {
    const std::string& sval = value.get_str();
    return boost::lexical_cast<float>(sval);
  }
  case null_type:
    return 0.0f;
  case int_type:
    return value.get_int();
  case real_type:
    return value.get_real();
  }
  parseError("invalid float value.");
  return 0.0f;
}

void CalculatorPage::_parseBitstamp(const std::string& str)
{
  Value valRequest;
  if (!read_string(str, valRequest))
  {
    parseError("Error parsing reply string");
    return;
  }
  if (valRequest.type() != obj_type)
  {
    parseError("Invalid reply object");
    return;
  }
  const Object& reply = valRequest.get_obj();
  {
    Value vvwap = find_value(reply, "vwap");
    _private->_bitcoinPriceUsd = parseFloat(vvwap);
    _bitcoinPriceUsdUpdated();
  }
}

void CalculatorPage::_parseMarketSummaries(const std::string& str)
{
  Value valRequest;
  if (!read_string(str, valRequest))
  {
    parseError("Error parsing reply string");
    return;
  }
  if (valRequest.type() != obj_type)
  {
    parseError("Invalid reply object");
    return;
  }
  const Object& reply = valRequest.get_obj();
  {
    Value success = find_value(reply, "success");
    if (success.type() != bool_type)
    {
      parseError("Invalid \"success\" field");
      return;
    }
    if (!success.get_bool())
    {
      parseError("\"success\"==false");
      return;
    }
  }
  {
    Value vresult = find_value(reply, "result");
    if (vresult.type() != array_type)
    {
      parseError("Invalid \"result\" field");
      return;
    }
    const Array& result = vresult.get_array();
    Array::const_iterator i = result.begin(), iend = result.end();
    for (; i != iend; ++i)
    {
      const Value &v = (*i);
      if (v.type() != obj_type)
      {
        parseError("Invalid \"result\" array element.");
        return;
      }
      const Object& o = v.get_obj();
      {
        Value vMarketName = find_value(o, "MarketName");
        if (vMarketName.type() != str_type)
        {
          parseError("Invalid \"result\" array element (MarketName).");
          return;
        }
        const QString marketName = QString::fromStdString(vMarketName.get_str());
        const int sz = BTC_PREFIX.size();
        if (marketName.startsWith(BTC_PREFIX))
        {
          const QString currName = marketName.mid(sz);
          const Value vbid = find_value(o, "Bid");
          if (vbid.type() == null_type)
          {
            // empty Bid sometimes.
            continue;
          }
          const float value = parseFloat(vbid);
          _private->_marketList[currName] = MarketEntry(value);
        }
      }
    }
  }
  _private->_marketList[BTC_TEXT] = MarketEntry(1.0f);
  _marketsUpdated();
}


void updateCbxMarkets(QComboBox* const cbx,
  const CalculatorPagePrivate::MarketList& marketList)
{
  {
    for (int i = 0; i < cbx->count(); ++i)
    {
      if (marketList.find(cbx->itemText(i)) == marketList.end())
      {
        cbx->removeItem(i);
      }
    }
  }
  {
    CalculatorPagePrivate::MarketList::const_iterator i = marketList.begin(),
    iend = marketList.end();
    for (; i != iend; ++i)
    {
      const QString& name = (*i).first;
      if (cbx->findText(name) == -1)
      {
        cbx->addItem(name);
      }
    }
  }
  cbx->model()->sort(0);
}

void CalculatorPage::_marketsUpdated()
{
  updateCbxMarkets(ui->cbxSrcCurrency, _private->_marketList);
  updateCbxMarkets(ui->cbxResultCurrency, _private->_marketList);
  ui->actionUpdateResult->trigger();
}

void CalculatorPage::_bitcoinPriceUsdUpdated()
{
  ui->lblBtcToUsd->setText(
    QString("1 BTC = %1 USD (<a href=\"http://www.bitstamp.net/\">Bitstamp</a>)")
    .arg(_private->_bitcoinPriceUsd));
  _private->_marketList[USD_TEXT] = MarketEntry(1 / _private->_bitcoinPriceUsd);
  _marketsUpdated();
}

float getValue(const QComboBox* const cbx,
  const CalculatorPagePrivate::MarketList& marketList)
{
  const CalculatorPagePrivate::MarketList::const_iterator i = 
    marketList.find(cbx->currentText());
  if (i == marketList.end())
  {
    return PRICE_INITIAL;
  }
  return (*i).second.value;
}

void CalculatorPage::on_actionUpdateResult_triggered()
{ 
  const float srcvalue = getValue(ui->cbxSrcCurrency, _private->_marketList);
  const float dstvalue = getValue(ui->cbxResultCurrency, _private->_marketList);
  const float result = srcvalue * ui->editSrcAmount->value() / dstvalue;
  if (std::isnan(result))
  {
    ui->labelResultAmount->setText("??");
  }
  else
  {
    ui->labelResultAmount->setText(QString::number(result, 'f', 8));
  }
}
