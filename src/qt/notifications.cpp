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

static const QString URL_MARKET_GETMARKETS = 
  "https://bittrex.com/api/v1.1/public/getmarkets";
static const QString URL_DISPLAY_FORMAT = 
  "https://bittrex.com/Market/Index?MarketName=%1";

Notifications::Notifications(QWidget *parent) :
  QWidget(parent),
  ui(new Ui::Notifications),
  _walletModel(0),
  _refreshDataTimer(0),
  _firstUpdate(true)
{
  ui->setupUi(this);
  BOOST_VERIFY(connect(&_netManager, SIGNAL(finished(QNetworkReply*)),
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
  _resheduleDataRefresh();
  try
  {
    if (reply->error() != QNetworkReply::NoError)
    {
      _parseError(reply->errorString().toLocal8Bit().constData());
    }
    const QByteArray arr(reply->readAll());
    _parseReply(std::string(arr.data(), arr.size()));
  }
  catch(const std::exception& e)
  {
    ui->labelLastUpdate->setStyleSheet("color: red");
    ui->labelLastUpdate->setText(QString("Error loading data: %1").arg(e.what()));
    return;
  }
  ui->labelLastUpdate->setStyleSheet("");
  ui->labelLastUpdate->setText(QString("Last updated: %1").
          arg(QDateTime::currentDateTime().toString()));
  _firstUpdate = false;
}

void Notifications::_parseError(const char* const what)
{
  throw std::logic_error(what);
}

void Notifications::_parseReply(const std::string& str)
{
  Value valRequest;
  bool updateFound = false;
  if (!read_string(str, valRequest))
  {
    _parseError("Error parsing reply string");
    return;
  }
  if (valRequest.type() != obj_type)
  {
    _parseError("Invalid reply object");
    return;
  }
  const Object& reply = valRequest.get_obj();
  {
    Value success = find_value(reply, "success");
    if (success.type() != bool_type)
    {
      _parseError("Invalid \"success\" field");
      return;
    }
    if (!success.get_bool())
    {
      _parseError("\"success\"==false");
      return;
    }
  }
  {
    Value vresult = find_value(reply, "result");
    if (vresult.type() != array_type)
    {
      _parseError("Invalid \"result\" field");
      return;
    }
    const Array& result = vresult.get_array();
    static const std::locale iloc(std::locale::classic(),
          new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%S.%F"));
    static const std::locale oloc(std::locale::classic(),
          new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%S"));
    Array::const_iterator i = result.begin(), iend = result.end();
    for(; i != iend; ++ i)
    {
      const Value &v = (*i);
      if (v.type() != obj_type)
      {
        _parseError("Invalid \"result\" array element.");
        return;
      }
      std::string dt;
      const Object& o = v.get_obj();
      {
        Value vCreated = find_value(o, "Created");
        if (vCreated.type() != str_type)
        {
          _parseError("Invalid \"result\" array element (Created).");
          return;
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
        {
          // write time to string.
          std::ostringstream ss;
          ss.imbue(oloc);
          typedef boost::date_time::c_local_adjustor<boost::posix_time::ptime> local_adj;
          ss << local_adj::utc_to_local(pt);
          dt = ss.str();
        }
      }
      {
        Value vMarketName = find_value(o, "MarketName");
        if (vMarketName.type() != str_type)
        {
          _parseError("Invalid \"result\" array element (MarketName).");
          return;
        }
        const std::string& marketName = vMarketName.get_str();
        const std::pair<KnownMarketList::iterator, bool> ir = 
          _knownMarketList.insert(marketName);
        if (!_firstUpdate && ir.second)
        {
          updateFound = true;
          const QString qm (QString::fromStdString(marketName));
          QTreeWidgetItem* itm (new QTreeWidgetItem(ui->listMarkets, 0));
          itm->setData(0, Qt::DisplayRole, QString::fromStdString(dt));
          itm->setData(1, Qt::DisplayRole, qm);
          itm->setData(2, Qt::DisplayRole,
                QString(URL_DISPLAY_FORMAT).arg(qm));
          ui->listMarkets->addTopLevelItem(itm);
        }
      }
    }
  }
  if (updateFound)
  {
    _notifyUpdateFound();
  }
}


void Notifications::_refreshData()
{
  _sendRequest(URL_MARKET_GETMARKETS);
}

void Notifications::notificationsEnabledChanged(bool value)
{
  if (value)
  {
    _resheduleDataRefresh();
  }
}

void Notifications::_sendRequest(const QString &url)
{
  QNetworkRequest request ((QUrl(url)));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
            "application/json; charset=utf-8");
  _netManager.get(request);
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

