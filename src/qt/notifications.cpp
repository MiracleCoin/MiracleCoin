#include "notifications.h"
#include "ui_notifications.h"
#include "walletmodel.h"
#include "optionsmodel.h"

#include <QTimer>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStringList>
#include <QDateTime>
#include <QDesktopServices>
#include <QSharedPointer>

#include <boost/assert.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/local_time_adjustor.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <iostream>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include <string>
#include <stdexcept>

using namespace json_spirit;

static const QString BTX_URL_MARKET_GETMARKETS =
  "https://bittrex.com/api/v1.1/public/getmarkets";
static const QString BTX_URL_DISPLAY_FORMAT =
  "https://bittrex.com/Market/Index?MarketName=%1";
static const QString CCEX_URL_MARKET_GETMARKETS =
  "https://c-cex.com/t/pairs.json";
static const QString CCEX_URL_DISPLAY_FORMAT =
    "https://c-cex.com/?p=%1";


class NotificationsPrivate;

class Parser
{
public:
  struct MarketEntry
  {
    MarketEntry(QString name, QDateTime creationDate, QString url):
      name(name), creationDate(creationDate), url(url) {}
    QString name;
    QDateTime creationDate;
    QString url;
  };
  typedef QList<MarketEntry> MarketList;
public:
  virtual MarketList parse(const std::string& data,
      const NotificationsPrivate& parent) = 0;
protected:
  void _parseError(const char* const what)
  {
    throw std::logic_error(what);
  }
};

class BtxParser : public Parser
{
public:
  virtual MarketList parse(const std::string& data,
        const NotificationsPrivate& parent);
};

class CcehParser : public Parser
{
public:
  virtual MarketList parse(const std::string& data,
    const NotificationsPrivate& parent);
};


class NotificationsPrivate
{
public:
  NotificationsPrivate(Notifications* parent) :
    _parent(parent),
    anyUpdateFound(false)
  {
    _registerParser<BtxParser>(BTX_URL_MARKET_GETMARKETS);
    _registerParser<CcehParser>(CCEX_URL_MARKET_GETMARKETS);
  }
  bool isMarketKnown(const QString& marketName) const
      { return _knownMarketList.contains(marketName); }
private:
  template <class T>
  void _registerParser(const QString& url)
  {
    _parserMap.insert(url, ParserMapEntry(new T));
  }
  void _sendRequests();
  void _sendRequest(const QString &url);
  struct ParserMapEntry;
  void _parseReply(ParserMapEntry& pe, const std::string& reply);
  void _requestFinished(QNetworkReply* reply);

private:
  Notifications* _parent;
  QNetworkAccessManager _netManager;
  typedef QSet<QString> KnownMarketList;
  KnownMarketList _knownMarketList;
  typedef QSet<QString> RequestPending;
  RequestPending _requestPending;
  typedef QSharedPointer<Parser> ParserPtr;
  struct ParserMapEntry
  {
    ParserMapEntry() : firstUpdated(false), updateFound(false){}
    ParserMapEntry(Parser* const parser) :
      firstUpdated(false), updateFound(false), parser(parser) {}
    ParserMapEntry(const ParserPtr parser) :
      firstUpdated(false), updateFound(false), parser(parser) {}
    bool firstUpdated;
    bool updateFound;
    ParserPtr parser;
  };
  typedef QMap<QString, ParserMapEntry> ParserMap;
  ParserMap _parserMap;
  bool anyUpdateFound;

  friend class Notifications;
};


Notifications::Notifications(QWidget *parent) :
  QWidget(parent),
  _private(new NotificationsPrivate(this)),
  ui(new Ui::Notifications),
 _refreshDataTimer(0),
 _walletModel(0)
{
  ui->setupUi(this);
  BOOST_VERIFY(connect(&_private->_netManager, SIGNAL(finished(QNetworkReply*)),
         this, SLOT(_requestFinished(QNetworkReply*)))
         );

  _refreshDataTimer = new QTimer(this);
  BOOST_VERIFY(
        connect(_refreshDataTimer, SIGNAL(timeout()),
                this, SLOT(_refreshData())));
  _refreshDataTimer->setSingleShot(true);
}

Notifications::~Notifications()
{
  delete _private;
  delete ui;
}

void Notifications::setModel(WalletModel *model)
{
  _walletModel = model;
  if (!_walletModel)
    return;

  OptionsModel* const omodel(_walletModel->getOptionsModel());
  if (!omodel)
    return;
  connect(omodel, SIGNAL(notificationsEnabledChanged(bool)),
          this, SLOT(notificationsEnabledChanged(bool)));
  _resheduleDataRefresh();
}

void Notifications::_requestFinished(QNetworkReply* reply)
{
  _private->_requestFinished(reply);
}

void Notifications::_newMarketFound(const QDateTime& date, const QString& name, const QString& url)
{
  QTreeWidgetItem* itm(new QTreeWidgetItem(ui->listMarkets, 0));
  itm->setData(0, Qt::DisplayRole, date);
//  itm->setData(0, Qt::DisplayRole, date.toString());
  itm->setData(1, Qt::DisplayRole, name);
  itm->setData(2, Qt::DisplayRole, url);
  ui->listMarkets->addTopLevelItem(itm);
}

void Notifications::_refreshData()
{
  _private->_sendRequests();
}

void Notifications::notificationsEnabledChanged(bool value)
{
  if (value)
  {
    _resheduleDataRefresh();
  }
}

void Notifications::_resheduleDataRefresh()
{
  if (_walletModel && _walletModel->getOptionsModel()
    && _walletModel->getOptionsModel()->getNotificationsEnabled())
  {
    _refreshDataTimer->start(10 * 1000);
  }
}


void Notifications::on_listMarkets_itemClicked(QTreeWidgetItem *item, int column)
{
  if (item && column == 2)
  {
    QDesktopServices::openUrl(item->data(2, Qt::DisplayRole).toString());
  }
}

void Notifications::_notifyUpdateFound()
{
  if (!_walletModel)
    return;

  OptionsModel* const omodel(_walletModel->getOptionsModel());
  if (!omodel)
    return;
  if (omodel->getNotificationsOpenPageEnabled()
    && !omodel->getNotificationsOpenPageUrl().isEmpty())
  {
    QDesktopServices::openUrl(omodel->getNotificationsOpenPageUrl());
  }
}

void NotificationsPrivate::_parseReply(ParserMapEntry& pe, const std::string& reply)
{
  Q_ASSERT(pe.parser);
  pe.updateFound = false;
  Parser::MarketList markets = pe.parser->parse(reply, *this);
  Q_FOREACH(const Parser::MarketEntry& market, markets)
  {
    const bool seen = isMarketKnown(market.name);
    if (!seen)
    {
      _knownMarketList.insert(market.name);
      if (pe.firstUpdated)
      {
        pe.updateFound = true;
        _parent->_newMarketFound(market.creationDate,
          market.name,
          market.url);
      }
    }
  }
  pe.firstUpdated = true;
}

void NotificationsPrivate::_sendRequest(const QString &url)
{
  QNetworkRequest request((QUrl(url)));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
    "application/json; charset=utf-8");
  _requestPending.insert(url);
  _netManager.get(request);
}

void NotificationsPrivate::_sendRequests()
{
  anyUpdateFound = false;
  for (ParserMap::const_iterator i = _parserMap.begin(); i != _parserMap.end(); ++ i)
  {
    _sendRequest(i.key());
  }
}


void NotificationsPrivate::_requestFinished(QNetworkReply* reply)
{
  reply->deleteLater();
  const QString urlString = reply->url().toString();
  _requestPending.remove(urlString);
  const bool finishedAll = _requestPending.empty();
  if (finishedAll)
  {
    _parent->_resheduleDataRefresh();
  }
  try
  {
    if (reply->error() != QNetworkReply::NoError)
    {
      throw std::logic_error(reply->errorString().toLocal8Bit().constData());
    }
    const QByteArray arr(reply->readAll());
    ParserMapEntry& pe = _parserMap[urlString];
    _parseReply(pe, std::string(arr.begin(), arr.end()));
    anyUpdateFound = anyUpdateFound || pe.updateFound;
    _parent->ui->labelLastUpdate->setStyleSheet("");
    _parent->ui->labelLastUpdate->setText(QString("Last updated: %1").
      arg(QDateTime::currentDateTime().toString()));
  }
  catch(const std::exception& e)
  {
    _parent->ui->labelLastUpdate->setStyleSheet("color: red");
    _parent->ui->labelLastUpdate->setText(QString("Error loading data: %1").arg(e.what()));
  }
  if (finishedAll && anyUpdateFound)
  {
    anyUpdateFound = false;
    _parent->_notifyUpdateFound();
  }
}

Parser::MarketList BtxParser::parse(const std::string& str, const NotificationsPrivate& parent)
{
  Value valRequest;
  MarketList result;
  if (!read_string(str, valRequest))
  {
    _parseError("Error parsing reply string");
    return result;
  }
  if (valRequest.type() != obj_type)
  {
    _parseError("Invalid reply object");
    return result;
  }
  const Object& reply = valRequest.get_obj();
  {
    Value success = find_value(reply, "success");
    if (success.type() != bool_type)
    {
      _parseError("Invalid \"success\" field");
      return result;
    }
    if (!success.get_bool())
    {
      _parseError("\"success\"==false");
      return result;
    }
  }
  {
    Value vresult = find_value(reply, "result");
    if (vresult.type() != array_type)
    {
      _parseError("Invalid \"result\" field");
      return result;
    }
    const Array& resultArr = vresult.get_array();
    static const std::locale iloc(std::locale::classic(),
          new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%S.%F"));
    static const std::locale oloc(std::locale::classic(),
          new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%S"));
    Array::const_iterator i = resultArr.begin(), iend = resultArr.end();
    for(; i != iend; ++ i)
    {
      const Value &v = (*i);
      if (v.type() != obj_type)
      {
        _parseError("Invalid \"result\" array element.");
        return result;
      }
      const Object& o = v.get_obj();
      {
        Value vCreated = find_value(o, "Created");
        if (vCreated.type() != str_type)
        {
          _parseError("Invalid \"result\" array element (Created).");
          return result;
        }
        //2014-08-19T07:57:56.893
        boost::posix_time::ptime pt;
        {
          // read time from string.
          std::istringstream ss;
          ss.imbue(iloc);
//          ss.exceptions(std::ios_base::failbit);
          ss.str(vCreated.get_str());
          ss >> pt;
        }
        QDateTime dt(QDate(pt.date().year(), pt.date().month(), pt.date().day()),
                    QTime(pt.time_of_day().hours(), pt.time_of_day().minutes(),
                      pt.time_of_day().seconds()));
        //{
        //  // write time to string.
        //  std::ostringstream ss;
        //  ss.imbue(oloc);
        //  typedef boost::date_time::c_local_adjustor<boost::posix_time::ptime> local_adj;
        //  ss << local_adj::utc_to_local(pt);
        //  dt = ss.str();
        //}
        Value vMarketName = find_value(o, "MarketName");
        if (vMarketName.type() != str_type)
        {
          _parseError("Invalid \"result\" array element (MarketName).");
          return result;
        }
        const QString marketName = QString::fromStdString(vMarketName.get_str());
        if (!parent.isMarketKnown(marketName))
        {
          result.append(MarketEntry(marketName,
              dt,
              QString(BTX_URL_DISPLAY_FORMAT).arg(marketName)));
        }
      }
    }
  }
  return result;
}


Parser::MarketList CcehParser::parse(const std::string& str, const NotificationsPrivate& parent)
{
  Value valRequest;
  MarketList result;
  if (!read_string(str, valRequest))
  {
    _parseError("Error parsing reply string");
    return result;
  }
  if (valRequest.type() != obj_type)
  {
    _parseError("Invalid reply object");
    return result;
  }
  const Object& reply = valRequest.get_obj();
  {
    Value vresult = find_value(reply, "pairs");
    if (vresult.type() != array_type)
    {
      _parseError("Invalid \"pairs\" field");
      return result;
    }
    const Array& resultArr = vresult.get_array();
    Array::const_iterator i = resultArr.begin(), iend = resultArr.end();
    for (; i != iend; ++i)
    {
      const Value &v = (*i);
      if (v.type() != str_type)
      {
        _parseError("Invalid \"pairs\" array element.");
        return result;
      }
      const QString pair = QString::fromStdString(v.get_str());
      {
        const QString marketName = pair;
        if (marketName.isEmpty())
        {
          _parseError("Invalid market pair");
        }
        if (!parent.isMarketKnown(marketName))
        {
          result.append(MarketEntry(marketName,
            QDateTime::currentDateTime(),
            QString(CCEX_URL_DISPLAY_FORMAT).arg(marketName)));
        }
      }
    }
  }
  return result;
}
