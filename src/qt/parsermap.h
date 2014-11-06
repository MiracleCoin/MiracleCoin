#pragma once

#include <QSharedPointer>
#include <QMap>

#include "parser.h"

#include <boost/tti/has_static_member_function.hpp>
#include <boost/tti/has_member_function.hpp>
#include <boost/static_assert.hpp>
#include <boost/utility/enable_if.hpp>

namespace parser {

struct ParserMapEntryBase
{
    ParserMapEntryBase(const bool autoUpdateEnabled = true):
        firstUpdated(false), autoUpdateEnabled(autoUpdateEnabled){}
    virtual ~ParserMapEntryBase() {}
    virtual void parseReply(const std::string& reply) = 0;
    virtual void prepareRequest(QNetworkRequest& req) = 0;
    virtual QString baseUrl() const = 0;
    virtual bool urlNeedArg() const = 0;
    virtual AutoUpdatePolicy autoUpdatePolicy() const = 0;
    virtual QString id() const = 0;
    bool firstUpdated;
    bool autoUpdateEnabled;
    QString arg;
};

typedef QSharedPointer<ParserMapEntryBase> ParserMapEntryPtr;
typedef QMap<QString, ParserMapEntryPtr> ParserMap;

BOOST_TTI_HAS_STATIC_MEMBER_FUNCTION(getUrl)
//BOOST_TTI_HAS_MEMBER_FUNCTION(prepareRequest)

//template<typename TParser>
//typename boost::enable_if<has_member_function_prepareRequest<TParser, void, boost::mpl::vector<QNetworkRequest&> > >::type
//callPrepareRequestIfExists(QSharedPointer<TParser> t, QNetworkRequest& req)
//{
//    t->prepareRequest(req);
//}

//template<typename TParser>
//typename boost::disable_if<has_member_function_prepareRequest<TParser, void, boost::mpl::vector<QNetworkRequest&> > >::type
//callPrepareRequestIfExists(QSharedPointer<TParser> /*t*/, QNetworkRequest& /*req*/)
//{
//}

template<class TParser>
struct ParserMapEntry: public ParserMapEntryBase
{
    // TParser must have static QString getUrl() function.
    BOOST_STATIC_ASSERT((has_static_member_function_getUrl<TParser, QString>::value));
    typedef typename TParser::result_type parser_result_type;
    typedef QSharedPointer<TParser> ParserPtr;

    // Creates entry, adds it into list and returns it.
    static ParserMapEntryPtr addNewEntry(
            ParserMap& map, parser_result_type& parserResult)
    {
        ParserMapEntryPtr p(new ParserMapEntry(parserResult));
        map[TParser::getUrl()] = p;
        return p;
    }

    ParserMapEntry(parser_result_type& parserResult):
        ParserMapEntryBase(TParser::autoUpdatePolicy() == AUTO_UPDATE_ALWAYS),
        parser(new TParser), parserResult(parserResult) {}
    virtual void parseReply(const std::string& reply)
    {
        Q_ASSERT(parser);
        parser->parse(reply, parserResult);
        firstUpdated = true;
    }
    virtual void prepareRequest(QNetworkRequest& req)
    {
        Q_ASSERT(parser);

        parser->prepareRequest(req);
//        callPrepareRequestIfExists(parser, req);
    }
    virtual QString baseUrl() const
    {
        return TParser::getUrl();
    }
    virtual bool urlNeedArg() const
    {
        return TParser::urlNeedArg();
    }
    virtual AutoUpdatePolicy autoUpdatePolicy() const
    {
        return TParser::autoUpdatePolicy();
    }
    virtual QString id() const
    {
        return TParser::id();
    }


    ParserPtr parser;
    parser_result_type& parserResult;
};


} //namespace