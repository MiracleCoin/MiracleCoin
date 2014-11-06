#include "parser.h"

#include <QNetworkRequest>

#include <boost/lexical_cast.hpp>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include <openssl/hmac.h>
#include <openssl/rand.h>

#include "optionsmodel.h"

using namespace json_spirit;


namespace parser{

static const QString BTX_URL_MARKET_GETMARKETS(
        "https://bittrex.com/api/v1.1/public/getmarkets");


static const QString BTX_URL_DISPLAY_FORMAT =
        "https://bittrex.com/Market/Index?MarketName=%1";
static const unsigned int BTX_DECIMALS = 8;
static const int64_t BTX_INT_MUL = 100000000;

static OptionsModel* optionsModel = 0;


void parseError(const char* const what)
{
    throw std::logic_error(what);
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
    default:
        parseError("invalid float value.");
        return 0.0f;
    }
}

void parseDigit(const char digit, int64_t& value)
{
    if (digit < '0' || digit > '9')
    {
        parseError("invalid float value.");
        return;
    }
    value *= 10;
    value += int(digit) - '0';
}

BitcoinValue parseBitcoinValue(const int value)
{
    return value * BTX_INT_MUL;
}

BitcoinValue parseBitcoinValue(const double value)
{
    return value * BTX_INT_MUL;
}

BitcoinValue parseBitcoinValue(const mValue& value)
{
    switch (value.type())
    {
    case str_type:
    {
        const std::string& strval = value.get_str();
        int64_t intpart = 0, fracpart = 0;
        bool isIntPart = true;
        int64_t fracMul = BTX_INT_MUL / 10;
        for(unsigned int i = 0; i < strval.size(); ++ i)
        {
            const char c = strval[i];
            if (c == '.' || c == ',')
            {
                isIntPart = false;
                continue;
            }
            parseDigit(c, isIntPart ? intpart : fracpart);
            if (!isIntPart)
            {
                fracMul /= 10;
                if (fracMul == 0)
                {
                    break;
                }
            }
        }
        const int64_t result = intpart * BTX_INT_MUL + fracpart;
        return result;
    }
    case int_type:
        return parseBitcoinValue(value.get_int());
    case real_type:
        return parseBitcoinValue(value.get_real());
    case null_type:
        return 0;
    default:
        parseError("invalid float value.");
        return 0;
    }
}

QString bitcoinValueToStr(const BitcoinValue bvalue)
{
    const int64_t intpart = bvalue / BTX_INT_MUL;
    const int64_t fracpart = bvalue % BTX_INT_MUL;
    std::string padding;
    for (int64_t i = (fracpart ? fracpart : 1 ) * 10; i <= BTX_INT_MUL; i *= 10)
    {
        padding.push_back('0');
    }
    std::stringstream ss;
    ss << intpart << "." << padding << fracpart;
    const QString result = QString::fromStdString(ss.str());;
    return result;
}

double bitcoinValueToDouble(const BitcoinValue bvalue)
{
    return bitcoinValueToStr(bvalue).toDouble();
}

BitcoinValue bitcoinValueMul(const BitcoinValue v1, const BitcoinValue v2)
{
    return BitcoinValue(double(v1) * double(v2) / BTX_INT_MUL);
}

BitcoinValue bitcoinValueDiv(const BitcoinValue dividend,
                             const BitcoinValue divider)
{
    return BitcoinValue(double(dividend) * BTX_INT_MUL / double(divider));
}

const mValue& fieldValue(const mObject& o, const std::string& name)
{
    return o.at(name);
}

std::string strValue(const mObject& o, const std::string& name)
{
    const mValue& v(fieldValue(o, name));
    if (v.is_null()) return std::string();
    return v.get_str();
}

bool boolValue(const mObject& o, const std::string& name)
{
    const mValue& v(fieldValue(o, name));
    if (v.is_null()) return false;
    return v.get_bool();
}

QDateTime parseDateTime(const std::string& str)
{
    //2014-08-19T07:57:56.893
    QDateTime result = QDateTime::fromString(
                QString::fromStdString(str), "yyyy-MM-ddTHH:mm:ss");
    result.setTimeSpec(Qt::UTC);
    result = result.toLocalTime();
    return result;
}

QDateTime dateTimeValue(const mObject& o, const std::string& name)
{
    const mValue& v(fieldValue(o, name));
    if (v.is_null()) return QDateTime();
    return parseDateTime(v.get_str());
}


BitcoinValue bitcoinValue(const mObject& o, const std::string& name)
{
    const mValue& v(fieldValue(o, name));
    return parseBitcoinValue(v);
}

mValue getResultFieldValue(const std::string& data)
{
    mValue resultField;
    mValue valRequest;
    if (!read_string(data, valRequest))
    {
        parseError("Error parsing reply string");
        return resultField;
    }
    if (valRequest.type() != obj_type)
    {
        parseError("Invalid reply object");
        return resultField;
    }
    const mObject& reply = valRequest.get_obj();
    {
        const mValue& success = reply.at("success");
        if (!success.get_bool())
        {
            mObject::const_iterator i = reply.find("message");
            if (i != reply.end())
            {
                parseError((*i).second.get_str().c_str());
            }
            else
            {
                parseError("\"success\"==false");
            }
            return resultField;
        }
    }
    resultField = reply.at("result");
    return resultField;
}

void BtxMarketParser::parse(const std::string& str, result_type& result)
{
    result.clear();
    mValue vresult(getResultFieldValue(str));
    {
        const mArray& resultArr = vresult.get_array();
        mArray::const_iterator i = resultArr.begin(), iend = resultArr.end();
        for(; i != iend; ++ i)
        {
            const mValue &v = (*i);
            const mObject& o = v.get_obj();
            {
                const bool isActive = o.at("IsActive").get_bool();
                if (!isActive)
                {
                    continue;
                }

                const QDateTime dt = parseDateTime(o.at("Created").get_str());
                const QString marketName = QString::fromStdString(o.at("MarketName").get_str());
                {
                    result.append(MarketEntry(marketName,
                                              dt,
                                              QString(BTX_URL_DISPLAY_FORMAT).arg(marketName)));
                }
            }
        }
    }
}

QString BtxMarketParser::getUrl()
{
    return BTX_URL_MARKET_GETMARKETS;
}


void BtxOrderListParser::parse(const std::string &data, result_type& result)
{
    result.clear();
    mValue vresult(getResultFieldValue(data));
    {
        const mArray& resultArr = vresult.get_array();
        mArray::const_iterator i = resultArr.begin(), iend = resultArr.end();
        for(; i != iend; ++ i)
        {
            const mValue &v = (*i);
            const mObject& o = v.get_obj();
            {
                OrderListEntry e;
                e.quantity = parseBitcoinValue(o.at("Quantity"));
                e.rate = parseBitcoinValue(o.at("Rate"));
                result.append(e);
            }
        }
    }
    qSort(result);
}

QString BtxOrderListParserSell::getUrl()
{
    return "https://bittrex.com/api/v1.1/public/getorderbook?market=BTC-%1&type=sell";
}

//template <class T>
//void fieldValue(const mObject& o, const std::string& name, T& result);
//template <>
void BtxOpenOrderParser::parse(const std::string& data, result_type& result)
{
    result.clear();
    const mValue resultValue = getResultFieldValue(data);
    if (resultValue.is_null())
    {
        return;
    }
    const mArray& resultArr = resultValue.get_array();
    mArray::const_iterator i = resultArr.begin(), iend = resultArr.end();
    for(; i != iend; ++ i)
    {
        const mObject& o = (*i).get_obj();
        {
            OpenOrder e((OpenOrder()));;
            e.uuid = strValue(o, "Uuid");
            e.orderUuid = strValue(o, "OrderUuid");
            e.exchange = strValue(o, "Exchange");
            e.orderType = getOrderType(strValue(o, "OrderType"));
            e.quantity = bitcoinValue(o, "Quantity");
            e.quantityRemaining = bitcoinValue(o, "QuantityRemaining");
            e.limit = bitcoinValue(o, "Limit");
            e.commissionPaid = bitcoinValue(o, "CommissionPaid");
            e.price = bitcoinValue(o, "Price");
            e.pricePerUnit = bitcoinValue(o, "PricePerUnit");
            e.opened = parseDateTime(strValue(o, "Opened"));
            e.closed = parseDateTime(strValue(o, "Closed"));
            result.append(e);
        }
    }
}

void BtxPrivateRequest::setOptionsModel(OptionsModel* model)
{
  optionsModel = model;
}

QString getApiKey()
{
  Q_ASSERT(optionsModel);
  return optionsModel->getBotsBittrexKey();
}

QString getApiSecret()
{
  Q_ASSERT(optionsModel);
  return optionsModel->getBotsBittrexSecret();
}

void BtxPrivateRequest::prepare(QNetworkRequest& req)
{
    if (!optionsModel)
    {
      parseError("No options model - Btx private functions disabled.");
    }
    enum { NONCE_BYTES = 4 };
    char nonce[NONCE_BYTES];

    RAND_pseudo_bytes((unsigned char *)nonce, NONCE_BYTES);
    QString nonceHex(QByteArray(nonce, NONCE_BYTES).toHex());

    const QString resultUrl = QString("%1&apikey=%2&nonce=%3").arg(req.url().toString()).arg(getApiKey()).arg(nonceHex);
    req.setUrl(QUrl(resultUrl));

    const QByteArray ba (req.url().toString().toLatin1());
    unsigned int hmacSize;

    const QString apiSecret = getApiSecret();
    const char* hmac = (const char*)HMAC(EVP_sha512(), apiSecret.toLocal8Bit(), apiSecret.size(),
                                         reinterpret_cast<const unsigned char*>(ba.constData()), ba.size(), NULL, &hmacSize);
    if (!hmac)
    {
        parseError("HMAC failed.");
    }
    const QByteArray apisign (QByteArray(hmac, hmacSize).toHex());
    req.setRawHeader("apisign", apisign);
}

void BtxOpenOrderParser::prepareRequest(QNetworkRequest& req)
{
    BtxPrivateRequest::prepare(req);
}

QString BtxOpenOrderParser::getUrl()
{
    return "https://bittrex.com/api/v1.1/market/getopenorders?market=BTC-%1";
}


void BtxPlaceOrderResultParser::parse(const std::string& data, Parser::result_type& result)
{
    result = result_type();
    const mValue resultValue = getResultFieldValue(data);
    if (resultValue.is_null())
    {
        return;
    }
    const mObject& o = resultValue.get_obj();
    result.uuid = strValue(o, "uuid");
}

void BtxPlaceOrderResultParser::prepareRequest(QNetworkRequest& req)
{
    BtxPrivateRequest::prepare(req);
}

QString BtxPlaceOrderResultParser::getUrl()
{
    return "https://bittrex.com/api/v1.1/market/selllimit?market=BTC-%1";
}


void BtxGetOrderResultParser::parse(const std::string& data, Parser::result_type& result)
{
    result = result_type();
    const mValue resultValue = getResultFieldValue(data);
    if (resultValue.is_null())
    {
        return;
    }
    const mObject& o = resultValue.get_obj();
    {
        result.accountId = strValue(o, "AccountId");
        result.orderUuid = strValue(o, "OrderUuid");
        result.exchange = strValue(o, "Exchange");
        result.type = getOrderType(strValue(o, "Type"));
        result.quantity = bitcoinValue(o, "Quantity");
        result.quantityRemaining = bitcoinValue(o, "QuantityRemaining");
        result.limit = bitcoinValue(o, "Limit");
        result.reserved = bitcoinValue(o, "Reserved");
        result.reserveRemaining = bitcoinValue(o, "ReserveRemaining");
        result.commissionReserved = bitcoinValue(o, "CommissionReserved");
        result.commissionReserveRemaining = bitcoinValue(o, "CommissionReserveRemaining");
        result.commissionPaid = bitcoinValue(o, "CommissionPaid");
        result.price = bitcoinValue(o, "Price");
        result.pricePerUnit = bitcoinValue(o, "PricePerUnit");
        result.opened = dateTimeValue(o, "Opened");
        result.closed = dateTimeValue(o, "Closed");
        result.isOpen = boolValue(o, "IsOpen");
        result.sentinel = strValue(o, "Sentinel");
        result.cancelInitiated = boolValue(o, "CancelInitiated");
        result.immediateOrCancel = boolValue(o, "ImmediateOrCancel");
        result.isConditional = boolValue(o, "IsConditional");
        result.condition = strValue(o, "Condition");
        result.conditionTarget = strValue(o, "ConditionTarget");
    }
}

void BtxGetOrderResultParser::prepareRequest(QNetworkRequest& req)
{
    BtxPrivateRequest::prepare(req);
}

QString BtxGetOrderResultParser::getUrl()
{
    return "https://bittrex.com/api/v1.1/account/getorder";
}


void BtxGetBalanceResultParser::parse(const std::string& data, Parser::result_type& result)
{
    result = result_type();
    const mValue resultValue = getResultFieldValue(data);
    if (resultValue.is_null())
    {
        return;
    }
    const mObject& o = resultValue.get_obj();
    {
        result.currency = strValue(o, "Currency");
        result.balance = bitcoinValue(o, "Balance");
        result.available = bitcoinValue(o, "Available");
        result.pending = bitcoinValue(o, "Pending");
        result.cryptoAddress = strValue(o, "CryptoAddress");
        result.requested = boolValue(o, "Requested");
        result.uuid = strValue(o, "Uuid");
    }
}

void BtxGetBalanceResultParser::prepareRequest(QNetworkRequest& req)
{
    BtxPrivateRequest::prepare(req);
}

QString BtxGetBalanceResultParser::getUrl()
{
    return "https://bittrex.com/api/v1.1/account/getbalance?currency=%1";
}


QString BtxGetBalanceResultParserBTC::getUrl()
{
    return "https://bittrex.com/api/v1.1/account/getbalance?currency=BTC";
}


void BtxCancelOrderResultParser::parse(const std::string& data, Parser::result_type& result)
{
    result = result_type();
    getResultFieldValue(data);
}

void BtxCancelOrderResultParser::prepareRequest(QNetworkRequest& req)
{
    BtxPrivateRequest::prepare(req);
}

QString BtxCancelOrderResultParser::getUrl()
{
    return "https://bittrex.com/api/v1.1/market/cancel";
}


} // namespace

