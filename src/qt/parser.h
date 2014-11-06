#pragma once

#include <QList>
#include <QDateTime>
#include <QString>

#include <stdexcept>
#include <typeinfo>
#include <stdint.h>

class QNetworkRequest;
class QDateTime;

class OptionsModel;

namespace parser {

// for summs and counts
typedef int64_t BitcoinValue;

enum AutoUpdatePolicy
{
    AUTO_UPDATE_NEVER,
    AUTO_UPDATE_ALWAYS,
    AUTO_UPDATE_MARKET_SELECTED, // when market is selected
    AUTO_UPDATE_RUNNING // when bot is running
};

QString bitcoinValueToStr(const BitcoinValue bvalue);
double bitcoinValueToDouble(const BitcoinValue bvalue);
BitcoinValue parseBitcoinValue(const int value);
BitcoinValue parseBitcoinValue(const double value);
BitcoinValue bitcoinValueMul(const BitcoinValue v1, const BitcoinValue v2);
BitcoinValue bitcoinValueDiv(const BitcoinValue dividend,
                             const BitcoinValue divider);


template <class T>
class Parser
{
public:
    typedef T result_type;
public:
    virtual ~Parser() {}
    virtual void parse(const std::string& data,
                       result_type& result) = 0;
    virtual void prepareRequest(QNetworkRequest& req)
    { Q_UNUSED(req); }
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_NEVER; }
    static QString id() { return typeid(Parser).name(); }
    static bool urlNeedArg() { return false; }
};

struct BtxPrivateRequest
{
  static void prepare(QNetworkRequest& req);
  static void setOptionsModel(OptionsModel* model);
};

struct MarketEntry
{
    MarketEntry(QString name, QDateTime creationDate, QString url):
        name(name), creationDate(creationDate), url(url) {}
    QString name;
    QDateTime creationDate;
    QString url;
};

class BtxMarketParser : public Parser<QList<MarketEntry> >
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    static QString getUrl();
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_ALWAYS; }
};



struct OrderListEntry
{
    BitcoinValue quantity;
    BitcoinValue rate;
    bool operator < (const OrderListEntry& other) const
    { return rate < other.rate; }
};

class BtxOrderListParser: public Parser<QList<OrderListEntry> >
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    static bool urlNeedArg() { return true; }
};

class BtxOrderListParserSell: public BtxOrderListParser
{
public:
    static QString getUrl();
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_RUNNING; }
};
enum OrderType
{
    ORDER_TYPE_UNKNOWN = 0,
    ORDER_TYPE_LIMIT_SELL,
    ORDER_TYPE_LIMIT_BUY
};

static OrderType getOrderType(const std::string& str)
{
    typedef std::map<std::string, OrderType> OrderTypeMap;
    static OrderTypeMap _map;
    if (_map.empty())
    {
        _map["LIMIT_SELL"] = ORDER_TYPE_LIMIT_SELL;
        _map["LIMIT_BUY"] = ORDER_TYPE_LIMIT_BUY;
    }
    OrderTypeMap::const_iterator i = _map.find(str);
    if (i == _map.end()) return ORDER_TYPE_UNKNOWN;
    return (*i).second;
}

// https://bittrex.com/api/v1.1/market/getopenorders?apikey=API_KEY&market=BTC-LTC
struct OpenOrder
{
    std::string uuid; // null,
    std::string orderUuid; // "09aa5bb6-8232-41aa-9b78-a5a1093e0211",
    std::string exchange; // "BTC-LTC",
    OrderType orderType; // "LIMIT_SELL",
    BitcoinValue quantity; // 5.00000000,
    BitcoinValue quantityRemaining; // 5.00000000,
    BitcoinValue limit; // 2.00000000,
    BitcoinValue commissionPaid; // 0.00000000,
    BitcoinValue price; // 0.00000000,
    std::string pricePerUnit; // null,
    QDateTime opened; // "2014-07-09T03:55:48.77",
    QDateTime closed; // null,
    bool cancelInitiated; // false,
    bool immediateOrCancel; // false,
    bool isConditional; // false,
    std::string condition; // null,
    std::string conditionTarget; // null
};

class BtxOpenOrderParser: public Parser<QList<OpenOrder> >
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    virtual void prepareRequest(QNetworkRequest& req);
    static QString getUrl();
    static bool urlNeedArg() { return true; }
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_RUNNING; }
};

struct PlaceOrderResult
{
    std::string uuid;
};

class BtxPlaceOrderResultParser: public Parser<PlaceOrderResult>
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    virtual void prepareRequest(QNetworkRequest& req);
    static QString getUrl();
    static bool urlNeedArg() { return true; }
    static QString id() { return "placeOrder"; }
};

// https://bittrex.com/api/v1.1/account/getorder&uuid=0cb4c4e4-bdc7-4e13-8c13-430e587d2cc1
struct Order
{
    std::string accountId; //  null,
    std::string orderUuid; //  "0cb4c4e4-bdc7-4e13-8c13-430e587d2cc1",
    std::string exchange; //  "BTC-SHLD",
    OrderType type; //  "LIMIT_BUY",
    BitcoinValue quantity; //  1000.00000000,
    BitcoinValue quantityRemaining; //  1000.00000000,
    BitcoinValue limit; //  0.00000001,
    BitcoinValue reserved; //  0.00001000,
    BitcoinValue reserveRemaining; //  0.00001000,
    BitcoinValue commissionReserved; //  0.00000002,
    BitcoinValue commissionReserveRemaining; //  0.00000002,
    BitcoinValue commissionPaid; //  0.00000000,
    BitcoinValue price; //  0.00000000,
    std::string pricePerUnit; //  null,
    QDateTime opened; //  "2014-07-13T07:45:46.27",
    QDateTime closed; //  null,
    bool isOpen; //  true,
    std::string sentinel; //  "6c454604-22e2-4fb4-892e-179eede20972",
    bool cancelInitiated; //  false,
    bool immediateOrCancel; //  false,
    bool isConditional; //  false,
    std::string condition; //  "NONE",
    std::string conditionTarget; //  null
};

class BtxGetOrderResultParser: public Parser<Order>
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    virtual void prepareRequest(QNetworkRequest& req);
    static QString getUrl();
    static QString id() { return "getOrder"; }
};

// https://bittrex.com/api/v1.1/account/getbalance?apikey=API_KEY&currency=BTC
struct Balance
{
    std::string currency; // "BTC",
    BitcoinValue balance; // 4.21549076,
    BitcoinValue available; // 4.21549076,
    BitcoinValue pending; // 0.00000000,
    std::string cryptoAddress; // "1MacMr6715hjds342dXuLqXcju6fgwHA31",
    bool requested; // false,
    std::string uuid; // null
};

class BtxGetBalanceResultParser: public Parser<Balance>
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    virtual void prepareRequest(QNetworkRequest& req);
    static QString getUrl();
    static bool urlNeedArg() { return true; }
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_RUNNING; }
    static QString id() { return "getBalance"; }
};


class BtxGetBalanceResultParserBTC: public BtxGetBalanceResultParser
{
public:
    static QString getUrl();
    static bool urlNeedArg() { return false; }
    static AutoUpdatePolicy autoUpdatePolicy() { return AUTO_UPDATE_RUNNING; }
    static QString id() { return "getBtcBalance"; }
};


// https://bittrex.com/api/v1.1/market/cancel?apikey=API_KEY&uuid=ORDER_UUID
struct CancelOrder
{
    std::string uuid;
};

class BtxCancelOrderResultParser: public Parser<CancelOrder>
{
public:
    virtual void parse(const std::string& data,
                       result_type& result);
    virtual void prepareRequest(QNetworkRequest& req);
    static QString getUrl();
    static QString id() { return "cancelOrder"; }
};

} // namespace

