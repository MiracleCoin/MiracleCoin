#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

/** Interface from Qt to configuration data structure for Bitcoin client.
   To Qt, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit OptionsModel(QObject *parent = 0);

    enum OptionID {
        StartAtStartup,    // bool
        MinimizeToTray,    // bool
        MapPortUPnP,       // bool
        MinimizeOnClose,   // bool
        ProxyUse,          // bool
        ProxyIP,           // QString
        ProxyPort,         // int
        ProxySocksVersion, // int
        Fee,               // qint64
        DisplayUnit,       // BitcoinUnits::Unit
        DisplayAddresses,  // bool
        DetachDatabases,   // bool
        Language,          // QString
        CoinControlFeatures, // bool
        NotificationsEnabled, // bool
        NotificationsOpenPageEnabled, // bool
        NotificationsOpenPageUrl, // QString
        BotsBittrexKey,           // QString
        BotsBittrexSecret,        // QString
        BotsBittrexEnabled,       // bool
        OptionIDRowCount,
    };

    void Init();

    /* Migrate settings from wallet.dat after app initialization */
    bool Upgrade(); /* returns true if settings upgraded */

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    /* Explicit getters */
    qint64 getTransactionFee();
    bool getMinimizeToTray();
    bool getMinimizeOnClose();
    int getDisplayUnit();
    bool getDisplayAddresses();
    bool getCoinControlFeatures();
    QString getLanguage() { return language; }
    bool getNotificationsEnabled() const { return fNotificationsEnabled; }
    bool getNotificationsOpenPageEnabled() const { return fNotificationsOpenPageEnabled; }
    QString getNotificationsOpenPageUrl() const { return fNotificationsOpenPageUrl; }
    QString getBotsBittrexKey() const { return fBotsBittrexKey; }
    QString getBotsBittrexSecret() const { return fBotsBittrexSecret; }
    bool getBotsBittrexEnabled() const { return fBotsBittrexEnabled; }

private:
    int nDisplayUnit;
    bool bDisplayAddresses;
    bool fMinimizeToTray;
    bool fMinimizeOnClose;
    bool fCoinControlFeatures;
    QString language;
    bool fNotificationsEnabled;
    bool fNotificationsOpenPageEnabled;
    QString fNotificationsOpenPageUrl;
    QString fBotsBittrexKey;
    QString fBotsBittrexSecret;
    bool fBotsBittrexEnabled;

signals:
    void displayUnitChanged(int unit);
    void transactionFeeChanged(qint64);
    void coinControlFeaturesChanged(bool);
    void notificationsEnabledChanged(bool);
    void botsBittrexEnabledChanged(bool);
};

#endif // OPTIONSMODEL_H
