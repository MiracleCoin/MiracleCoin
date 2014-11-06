#include "sellbot.h"
#include "ui_sellbot.h"
#include "parsermap.h"

#include <QTimer>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QDesktopServices>
#include <QSharedPointer>
#include <QThread>
#include <QDateTime>
#include <QPointer>
#include <QObject>
#include <QUrlQuery> // from 5.0

#include <boost/assert.hpp>

#include <string>
#include <stdexcept>

#include "optionsmodel.h"

using namespace parser;
using namespace sellbot;

namespace sellbot {
static const QString BTC_PAIR_PREFIX = "BTC-";

struct BlockSignalsGuard
{
    BlockSignalsGuard(QObject* obj):
        _obj(obj)

    {
        Q_ASSERT(_obj);
        _prevState = _obj->blockSignals(true);
    }
    ~BlockSignalsGuard() { if (_obj) _obj->blockSignals(_prevState); }
private:
    QPointer<QObject> _obj;
    bool _prevState;
};

template <typename T>
struct VarGuard
{
    VarGuard(T& val): oldVal(val), val(val) {}
    ~VarGuard() { val = oldVal; }
    T oldVal;
    T& val;
};

class Notifications;

typedef QMap<QString, QString> RequestParams;

class SellBotPrivate: public QObject
{
    Q_OBJECT
public:
    SellBotPrivate(SellBot* const parent);
    ~SellBotPrivate();
private Q_SLOTS:
    void _allDataUpdated();
    void _dataUpdated(const QString& baseUrl);
    void _dataUpdateError(const QString& baseUrl, const QString& msg);
    void _invokeFinishedMethod(const QString& baseUrl, const QString& error);

    // parser.id() + "_finished"
    void getBtcBalance_finished(const QString& error);
    void getBalance_finished(const QString& error);
    void getOrder_finished(const QString& error);
    void placeOrder_finished(const QString& error);
    void cancelOrder_finished(const QString& error);
    void stopTrade();
    void init();
    void cleanup();
private:
    void _updateMarketList();
    void _log(const QString& text);
    void _toggleTrade();
    void _selectedMarketChanged(const QString& market);
    bool _isMyOrder(const OrderListEntry& entry);
    BitcoinValue _getMinAskNotMy();
    void _updateOpenOrder();

    template <class TParser>
    void _sendRequest(const RequestParams& params);

    void _placeOrder(const BitcoinValue ask);
    void _cancelOrder(const QString& id);
    void _getOrder(const QString& id);
private:
    SellBot* _parent;
    Notifications* _notifications;
    bool _running;
    bool _totalLimitFirstUpdated;
    QString _market;
    QString _orderId;
    BitcoinValue _orderRate;
    BitcoinValue _orderQuantity;
    BitcoinValue _orderQuantityRemaining;
    QString _waitingResultBaseUrl;

    friend class ::SellBot;
};

class Notifications: public QObject
{
    Q_OBJECT

public:
    Notifications(QObject* parent = 0) :
        QObject(parent),
        _refreshDataTimer(0)
    {
    }
    ~Notifications()
    {
    }

    template<class TParser>
    bool firstUpdated() const {
        const ParserMapEntryPtr p =_parserMap[TParser::getUrl()];
        Q_ASSERT(p);
        return p->firstUpdated;
    }

    template<class TParser>
    void setParserEnabled(const bool autoUpdateEnabled) const {
        ParserMapEntryPtr p =_parserMap[TParser::getUrl()];
        Q_ASSERT(p);
        p->autoUpdateEnabled = autoUpdateEnabled;
        p->firstUpdated = false;
    }

    void sendRequest(const ParserMapEntryPtr pe,
                      const RequestParams& params = RequestParams());
public Q_SLOTS:
    void selectedMarketChanged(const QString& market);

Q_SIGNALS:
    void allUpdated();
    void updateError(const QString& baseUrl, const QString& msg);
    void updated(const QString& baseUrl);
private:
    void _registerParsers()
    {
        _registerParser<BtxMarketParser>(_marketList);
        _registerParser<BtxOrderListParserSell>(_sellOrderList);
        _registerParser<BtxOpenOrderParser>(_openOrderList);
        _registerParser<BtxPlaceOrderResultParser>(_placeOrderResult);
        _registerParser<BtxGetOrderResultParser>(_order);
        _registerParser<BtxGetBalanceResultParser>(_balanceMarket);
        _registerParser<BtxGetBalanceResultParserBTC>(_balanceBtc);
        _registerParser<BtxCancelOrderResultParser>(_canceledOrder);
    }
    template <class TParser, class TResult>
    void _registerParser(TResult& result)
    {
        ParserMapEntry<TParser>::addNewEntry(_parserMap, result);
//        _parserMap.insert(url, ParserMapEntryPtr(new ParserMapEntry<T>(new T, list, enabled)));

    }
    void _sendRequests();
    void _doSendRequest(const ParserMapEntryPtr pe,
                      const RequestParams& params = RequestParams());
    void _resheduleDataRefresh()
    {
        _refreshDataTimer->start(3 * 1000);
    }
private Q_SLOTS:
    void _requestFinished(QNetworkReply* reply);
    void _refreshData()
    {
        _sendRequests();
    }
    void _init()
    {
      if (_inited())
      {
        return;
      }
      _registerParsers();

        _refreshDataTimer = new QTimer(this);
        BOOST_VERIFY(
                    connect(_refreshDataTimer, SIGNAL(timeout()),
                            this, SLOT(_refreshData())));
        _refreshDataTimer->setSingleShot(true);

        BOOST_VERIFY(connect(&_netManager, SIGNAL(finished(QNetworkReply*)),
                             this, SLOT(_requestFinished(QNetworkReply*)))
                     );
        _refreshData();
    }
    void _cleanup()
    {
      if (!_inited())
      {
        return;
      }
      delete _refreshDataTimer;
      _refreshDataTimer = 0;
      _parserMap.clear();
    }
    bool _inited() const { return _refreshDataTimer;}
private:
    QNetworkAccessManager _netManager;
    typedef ParserMap RequestMap;
    RequestMap _requestSent;
    typedef QMap<ParserMapEntryPtr, RequestParams> RequestQueue;
    RequestQueue _requestQueue;
    ParserMap _parserMap;
    QTimer* _refreshDataTimer;
    QList<MarketEntry> _marketList;
    QList<OrderListEntry> _sellOrderList;
    QList<OpenOrder> _openOrderList;
    PlaceOrderResult _placeOrderResult;
    Order _order;
    Balance _balanceMarket;
    Balance _balanceBtc;
    CancelOrder _canceledOrder;

    friend class SellBotPrivate;
};

#include "sellbot.moc"

void setCurrencyEditParms(QDoubleSpinBox* edit)
{
    Q_ASSERT(edit);
    static const float CURR_MIN = 0.0005f;
    edit->setMinimum(CURR_MIN);
    edit->setSingleStep(CURR_MIN / 10);
}

QUrl urlWithParams(const QString& urlStr, const RequestParams& params)
{
    QUrl result(urlStr);
    if (!params.isEmpty())
    {
        QUrlQuery qr(result);
        RequestParams::const_iterator i = params.begin(), iend = params.end();
        for (; i != iend; ++ i)
        {
            qr.addQueryItem(i.key(), i.value());
        }
        result.setQuery(qr);

    }
    return result;
}


QString urlWithArg(const QString& urlStr, const QString& arg)
{
    if (!arg.isEmpty())
        return urlStr.arg(arg);
    return urlStr;
}

void Notifications::_doSendRequest(const ParserMapEntryPtr pe,
                                 const RequestParams& params)
{
    const QUrl qurl(urlWithParams(urlWithArg(pe->baseUrl(), pe->arg), params));
    QNetworkRequest request(qurl);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/json; charset=utf-8");
    pe->prepareRequest(request);
    _requestSent.insert(request.url().toString(), pe);
    _netManager.get(request);
}

void Notifications::_sendRequests()
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    for (ParserMap::const_iterator i = _parserMap.begin(); i != _parserMap.end(); ++ i)
    {
        const ParserMapEntryPtr& value = i.value();
        if (value->autoUpdateEnabled)
        {
            _doSendRequest(value);
        }
    }
    for (RequestQueue::const_iterator i = _requestQueue.begin(); i != _requestQueue.end(); ++ i)
    {
        _doSendRequest(i.key(), i.value());
    }
    _requestQueue.clear();
}

void Notifications::sendRequest(const ParserMapEntryPtr pe, const RequestParams& params)
{
    _requestQueue[pe] = params;
}

void updateParserArg(ParserMapEntryPtr p, const QString& arg)
{
    Q_ASSERT(p);
    p->firstUpdated = false;
    p->arg = arg;
    if (arg.isEmpty())
    {
        p->autoUpdateEnabled = false;
    }
    else
    {
        if (p->autoUpdatePolicy() == AUTO_UPDATE_MARKET_SELECTED)
        {
            p->autoUpdateEnabled = true;
        }
    }
}

void Notifications::selectedMarketChanged(const QString& market)
{
    Q_ASSERT(QThread::currentThread() == this->thread());
    Q_FOREACH(ParserMapEntryPtr p, _parserMap)
    {
        if (p->urlNeedArg()) updateParserArg(p, market);
    }
}

void invokeFinishedMethod(QObject*obj, const QString& id, const QString& error)
{
    if (obj)
    {
        const QByteArray nm(id.toLatin1() + "_finished");
        const QByteArray sig(QMetaObject::normalizedSignature(nm + "(QString)"));
        if (obj->metaObject()->indexOfSlot(sig) != -1)
        {
            QMetaObject::invokeMethod(obj, nm, Q_ARG(QString, error));
        }
    }
}

void Notifications::_requestFinished(QNetworkReply* reply)
{
    const QString urlString = reply->url().toString();
    ParserMapEntryPtr pe = _requestSent[urlString];
    _requestSent.remove(urlString);
    const bool finishedAll = _requestSent.empty();
    try
    {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
        {
            throw std::logic_error(reply->errorString().toLocal8Bit().constData());
        }
        const QByteArray arr(reply->readAll());
        if (!pe)
        {
            throw std::logic_error("Unknown url to parse");
        }
        {
            pe->parseReply(std::string(arr.begin(), arr.end()));
        }
        Q_EMIT updated(pe->baseUrl());
        //invokeFinishedMethod(parent(), pe->id(), true);

    }
    catch(const std::exception& e)
    {
        const QString finalUrlString = pe ? pe->baseUrl() : urlString;
        Q_EMIT updateError(finalUrlString, e.what());
//        if (pe)
//        {
//            invokeFinishedMethod(parent(), pe->id(), false);
//        }
    }
    if (finishedAll)
    {
        Q_EMIT allUpdated();
        _resheduleDataRefresh();
    }
}

SellBotPrivate::SellBotPrivate(SellBot* const parent):
    QObject(parent),
    _parent(parent),
    _notifications(new Notifications(this)),
    _running(false),
    _totalLimitFirstUpdated(false)
{
    BOOST_VERIFY(
                connect(_notifications, SIGNAL(updated(QString)),
                        this, SLOT(_dataUpdated(QString))));
    BOOST_VERIFY(
                connect(_notifications, SIGNAL(allUpdated()),
                        this, SLOT(_allDataUpdated())));
    BOOST_VERIFY(
                connect(_notifications, SIGNAL(updateError(QString, QString)),
                        this, SLOT(_dataUpdateError(QString, QString))));
    qsrand(QDateTime::currentDateTime().toTime_t());
}

SellBotPrivate::~SellBotPrivate()
{
    delete _notifications;
}

void SellBotPrivate::init()
{
  Q_ASSERT(_notifications);
  _notifications->_init();
}

void SellBotPrivate::cleanup()
{
  Q_ASSERT(_notifications);
  stopTrade();
  _notifications->_cleanup();
}

void SellBotPrivate::stopTrade()
{
  if (_running)
  {
    _toggleTrade();
  }
}

void SellBotPrivate::_allDataUpdated()
{
    _updateMarketList();
    if (_running && _notifications->firstUpdated<BtxOrderListParserSell>()
            && _notifications->firstUpdated<BtxOpenOrderParser>())
    {
        _updateOpenOrder();
        if (_orderId.isEmpty())
        {
            if (_waitingResultBaseUrl.isEmpty())
            {
                const BitcoinValue minAsk = _getMinAskNotMy();
                if (minAsk <= 0)
                {
                  _log("No good orders found for price calculation.");
                  stopTrade();
                  return;
                }
                const BitcoinValue ask = minAsk - 1;
                if(_orderId.isEmpty())
                {
                    _log(QString("Placing order with ask=%1").arg(bitcoinValueToStr(ask)));
                    _placeOrder(ask);
                }

            }
        }
    }
}

void SellBotPrivate::_dataUpdated(const QString& baseUrl)
{
    if (_waitingResultBaseUrl == baseUrl)
    {
        _waitingResultBaseUrl.clear();
    }
    _invokeFinishedMethod(baseUrl, QString());
}

void SellBotPrivate::_dataUpdateError(const QString &baseUrl, const QString& error)
{
    _log(QString("Error %1 (%2)").arg(error).arg(QString(baseUrl).replace("%1", "")));
    if (_waitingResultBaseUrl == baseUrl)
    {
        _waitingResultBaseUrl.clear();
    }
    _invokeFinishedMethod(baseUrl, error.isEmpty() ? "<empty error>" : error);
    if (error == "INSUFFICIENT_FUNDS")
    {
      stopTrade();
    }
    else if (error == "APIKEY_INVALID")
    {
      stopTrade();
    }
}

void SellBotPrivate::_invokeFinishedMethod(const QString& baseUrl, const QString& error)
{
    Q_ASSERT(_notifications);
    ParserMapEntryPtr pe = _notifications->_parserMap[baseUrl];
    Q_ASSERT(pe);
    invokeFinishedMethod(this, pe->id(), error);
}

void SellBotPrivate::getBtcBalance_finished(const QString& error)
{
    if (error.isEmpty())
    {
        const QString btcStr = bitcoinValueToStr(_notifications->_balanceBtc.available);
        _parent->ui->labelBtcCount->setText(btcStr);
        if (!_totalLimitFirstUpdated)
        {
            _totalLimitFirstUpdated = true;
//            _parent->ui->editTotalSellLimit->setValue(btcStr.toDouble());
        }
    }

//    ;
//    _log(QString("BTC: %1").arg(QString::fromStdString(bitcoinValueToStr(_notifications->_balanceBtc.balance))));
}

void SellBotPrivate::getBalance_finished(const QString& error)
{
    if (error.isEmpty())
    {
        _parent->ui->labelMarketCount->setText(
                bitcoinValueToStr(_notifications->_balanceMarket.available));
        _parent->ui->labelMarketName->setText(_market);
    }
}

void SellBotPrivate::getOrder_finished(const QString& error)
{
    Q_ASSERT(_notifications);
    Q_ASSERT(_parent);
    if (error.isEmpty())
    {
        Q_ASSERT(_notifications->_order.type == ORDER_TYPE_LIMIT_SELL);
        if (_orderId == QString::fromStdString(_notifications->_order.orderUuid))
        {
            const BitcoinValue delta = (_orderQuantityRemaining -
                _notifications->_order.quantityRemaining);
            if (delta > 0)
            {
                const BitcoinValue deltaBtc = bitcoinValueMul(delta, _orderRate);
                _log(QString("Sell %1 %2 (%3 BTC)").
                            arg(bitcoinValueToStr(delta)).arg(_market).
                            arg(bitcoinValueToStr(deltaBtc)));
                double newLimit = _parent->ui->editTotalSellLimit->value() -
                        bitcoinValueToDouble(deltaBtc);
                if (newLimit <= 0)
                {
                    newLimit = 0.0;
                    _log("Total BTC limit reached.");
                    stopTrade();
                }
                _parent->ui->editTotalSellLimit->setValue(newLimit);
            }
            _orderQuantityRemaining = _notifications->_order.quantityRemaining;
        }

        if (!_notifications->_order.isOpen ||
                _notifications->_order.cancelInitiated)
        {
            _orderId.clear();
            _parent->ui->labelOrderRate->clear();
            _parent->ui->labelOrderSize->clear();
        }
        else
        {
            _orderId = QString::fromStdString(_notifications->_order.orderUuid);
            _orderQuantity = _notifications->_order.quantity;
            _orderRate = _notifications->_order.limit;
            _orderQuantityRemaining = _notifications->_order.quantityRemaining;

            _parent->ui->labelOrderRate->setText(bitcoinValueToStr(_orderRate));
            _parent->ui->labelOrderSize->setText(bitcoinValueToStr(_orderQuantityRemaining));

            if (!_waitingResultBaseUrl.isEmpty())
            {
                return;
            }
            const BitcoinValue minAsk = _getMinAskNotMy();
            if (minAsk <= _orderRate)
            {
                _log(QString("Canceling order (found Ask %1) id=%2").
                     arg(bitcoinValueToStr(minAsk)).arg(_orderId));
                _cancelOrder(_orderId);
            }
        }
    }
    else
    {
        _orderId.clear();
    }
}

void SellBotPrivate::placeOrder_finished(const QString& error)
{
    Q_ASSERT(_notifications);
    if (error.isEmpty())
    {
        _orderId = QString::fromStdString(_notifications->_placeOrderResult.uuid);
    }
}

void SellBotPrivate::cancelOrder_finished(const QString& /*error*/)
{
    _orderId.clear();
}

void SellBotPrivate::_updateMarketList()
{
    Q_ASSERT(_parent);
    {
        BlockSignalsGuard b(_parent->ui->listMarkets);
        const QString oldText = _parent->ui->listMarkets->currentText();
        _parent->ui->listMarkets->clear();
        Q_FOREACH(const MarketEntry& i,_notifications->_marketList)
        {
            // add only "BTC-XXX" markets
            if (i.name.startsWith(BTC_PAIR_PREFIX))
            {
                _parent->ui->listMarkets->addItem(i.name.right(i.name.size() - BTC_PAIR_PREFIX.size()));
            }
        }
        _parent->ui->listMarkets->model()->sort(0);
        _parent->ui->listMarkets->setCurrentText(oldText);
        if (_parent->ui->listMarkets->currentText() != oldText)
        {
            _parent->ui->listMarkets->setCurrentIndex(-1);
        }
    }
}

void SellBotPrivate::_log(const QString &text)
{
    Q_ASSERT(_parent);
    const int cnt = _parent->ui->listLog->count();
    const bool lastSelected = _parent->ui->listLog->selectedItems().count() > 0 &&
            _parent->ui->listLog->selectedItems()[0] ==
            _parent->ui->listLog->item(cnt - 1);

    _parent->ui->listLog->addItem(QString("%1: %2").
                                  arg(QDateTime::currentDateTime().
                                      toString(Qt::DefaultLocaleShortDate)).
                                  arg(text));
    if (cnt >= 3000)
    {
        delete(_parent->ui->listLog->takeItem(0));
    }
    if (lastSelected || _parent->ui->listLog->selectedItems().empty())
    {
        if (lastSelected)
        {
            _parent->ui->listLog->item(_parent->ui->listLog->count() - 1)->setSelected(true);
        }
        _parent->ui->listLog->scrollToBottom();
    }
}

void SellBotPrivate::_toggleTrade()
{
    Q_ASSERT(_notifications);
    if (_running)
    {
        if (!_orderId.isEmpty())
        {
            _cancelOrder(_orderId);
        }
    }
    _running = !_running;
    _parent->updateControls();
    Q_FOREACH(ParserMapEntryPtr p, _notifications->_parserMap)
    {
        if (p->autoUpdatePolicy() == AUTO_UPDATE_RUNNING)
        {
            p->autoUpdateEnabled = _running;
        }
    }
    if (_running)
    {
        _log(QString(">>>>> Start trading on market: %1").arg(_market));
    }
    else
    {
        _log(QString("<<<<< Stop trading on market: %1").arg(_market));
    }
}

void SellBotPrivate::_selectedMarketChanged(const QString& market)
{
    Q_ASSERT(!_running);
    _market = market;
    _parent->ui->labelMarketName->setText(_market);
    BOOST_VERIFY(
        QMetaObject::invokeMethod(_notifications, "selectedMarketChanged",
                                  Q_ARG(QString, market)));
}

bool SellBotPrivate::_isMyOrder(const OrderListEntry& entry)
{
    return entry.quantity == _orderQuantity && entry.rate == _orderRate;
}

BitcoinValue SellBotPrivate::_getMinAskNotMy()
{
    Q_ASSERT(_notifications);
    Q_FOREACH(const OrderListEntry& e, _notifications->_sellOrderList)
    {
        if (_isMyOrder(e))
        {
            continue;
        }
        return e.rate;
    }
    return 0;
}

void SellBotPrivate::_updateOpenOrder()
{
    Q_ASSERT(_notifications);
    if (!_waitingResultBaseUrl.isEmpty())
    {
        return;
    }
    Q_FOREACH(const OpenOrder& e, _notifications->_openOrderList)
    {
        if (e.orderType == ORDER_TYPE_LIMIT_SELL)
        {
            _getOrder(QString::fromStdString(e.orderUuid));
            return;
        }
    }
    if (!_orderId.isEmpty())
    {
        _getOrder(_orderId);
    }
}

void SellBotPrivate::_placeOrder(const BitcoinValue ask)
{
    Q_ASSERT(_running);
    Q_ASSERT(_parent);
    BitcoinValue orderSize = parseBitcoinValue(_parent->ui->editOrderLimit->value());
    int deviation = _parent->ui->editDeviation->value();
    if (deviation)
    {
      deviation = qrand() % deviation;
    }
    orderSize += double(orderSize) * deviation / 100.0;
    const BitcoinValue quantity = bitcoinValueDiv(orderSize, ask);
    RequestParams parms;
    parms["quantity"] = bitcoinValueToStr(quantity);
    parms["rate"] = bitcoinValueToStr(ask);
    _orderQuantity = quantity;
    _orderQuantityRemaining = quantity;
    _orderRate = ask;
    _sendRequest<BtxPlaceOrderResultParser>(parms);
}

void SellBotPrivate::_cancelOrder(const QString& id)
{
    Q_ASSERT(_running);
    Q_ASSERT(_parent);
    RequestParams parms;
    parms["uuid"] = id;
    _sendRequest<BtxCancelOrderResultParser>(parms);
}

void SellBotPrivate::_getOrder(const QString& id)
{
    Q_ASSERT(_running);
    Q_ASSERT(_parent);
    RequestParams parms;
    parms["uuid"] = id;
    _sendRequest<BtxGetOrderResultParser>(parms);
}

template <class TParser>
void SellBotPrivate::_sendRequest(const RequestParams& params)
{
    Q_ASSERT(_notifications);
    ParserMapEntryPtr pe = _notifications->_parserMap[TParser::getUrl()];
    Q_ASSERT(pe);
//    Q_ASSERT(_waitingResultBaseUrl.isEmpty());
    _waitingResultBaseUrl = pe->baseUrl();
    _notifications->sendRequest(pe, params);
}

} // namespace

SellBot::SellBot(QWidget *parent) :
QWidget(parent),
ui(new Ui::SellBot),
_private(new SellBotPrivate(this)),
_optionsModel(0)
{
  ui->setupUi(this);
  updateControls();
  setCurrencyEditParms(ui->editOrderLimit);
  setCurrencyEditParms(ui->editTotalSellLimit);
  ui->editTotalSellLimit->setMinimum(0.0);
  ui->editDeviation->setValue(10);
  ui->labelMarketName->clear();
  ui->labelMarketCount->clear();
  ui->labelBtcCount->clear();
  ui->labelOrderRate->clear();
  ui->labelOrderSize->clear();
}

SellBot::~SellBot()
{
  delete ui;
}

void SellBot::updateControls()
{
  Q_ASSERT(_private);
  if (_private->_running)
  {
    ui->buttonTrade->setText("Stop");
  }
  else
  {
    ui->buttonTrade->setText("Trade");
  }
  const bool marketSelected = ui->listMarkets->currentIndex() >= 0;
  ui->buttonTrade->setEnabled(marketSelected);
  ui->frameControls->setEnabled(!_private->_running);
}
void SellBot::on_buttonTrade_clicked()
{
  Q_ASSERT(_private);
  _private->_toggleTrade();
}

void SellBot::on_listMarkets_currentIndexChanged(const QString &market)
{
  Q_ASSERT(_private);
  _private->_selectedMarketChanged(market);
}

void SellBot::setOptionsModel(OptionsModel* model)
{
  Q_ASSERT(_private);
  _optionsModel = model;
  BtxPrivateRequest::setOptionsModel(model);
  if (_optionsModel)
  {
    BOOST_VERIFY(
      connect(_optionsModel, SIGNAL(botsBittrexEnabledChanged(bool)),
          this, SLOT(botsBittrexEnabledChanged(bool))));
    botsBittrexEnabledChanged(_optionsModel->getBotsBittrexEnabled());
  }
}

void SellBot::botsBittrexEnabledChanged(const bool enabled)
{
  ui->frameControls->setEnabled(enabled);
  if (enabled)
  {
    _private->init();
  }
  else
  {
    _private->cleanup();
  }
}

