#ifndef REQUESTAPI_H
#define REQUESTAPI_H
#include "logger.h"
#include <QDateTime>
#include <QQueue>
#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QHash>
#include <QMutex>
#include <QJsonObject>

struct PendingInfo {
    std::string weight;
    int car_id;
    int supply_id;
    QDateTime ts;   // 请求时间，用于超时
    QNetworkReply* reply; // optional, for correlation
};
using ReqItem = QPair<QNetworkRequest, QByteArray>;

class RequestAPI :public QObject
{
    Q_OBJECT
public:
    explicit RequestAPI(QObject* parent = nullptr);
    ~RequestAPI() override;
    void log(const std::string msg)
    {
        Logger::getInstance().Log(msg);
    }
    void enqueueOrSend(const QNetworkRequest& req, const QByteArray& payload, const QString& reqTag, int maxRetries, bool bypassPause = false);
    void tryStartNext();
    QString buildSignatureMd5(const QByteArray& payload) const;

private:
    mutable QMutex m_mutex;
    QNetworkAccessManager* m_netMgr = nullptr;
    QQueue<ReqItem> m_requestQueue;
    QHash<QNetworkReply*, qint64> m_pending; // reply -> start timestamp (ms)
    int requestTimeoutMs = 5000;    // 超时阈值（毫秒）
    int concurrencyLimit = 4;
    int maxQueueSize = 200;
    QTimer timeoutTimer;

    QString _getSlotUrl = "http://api.zygp.site/openapi/express/fjUpload";
    QString _uploadDropPackageUrl = "http://api.zygp.site/openapi/express/ytoJbUpload";
    QString _appkey = "gufv308hwnttxf6a";
    QString _appSecret = "n2lbmhea4qc2uvht64rq8aiy8c1lgit2";


private slots:
    void onNetworkFinished(QNetworkReply* reply); //所有的回调
    void checkPendingTimeouts();
    void requestForSlot(const QString& code);
    void uploadDropPackage(const QString& code);
signals:
    void getSlotSuccess(const QString& code, int slot_id);
    void requestFailed(const QString& reqUrl, const QString& errMsg);
};


#endif // REQUESTAPI_H
