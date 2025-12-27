#include "requestapi.h"
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QUuid>
#include <QUrl>
#include <QCoreApplication>
#include <QThread>
#include <QJsonArray>
extern std::uint64_t currentTimeMillis();
RequestAPI::RequestAPI(QObject* parent)
    : QObject(parent),
    m_netMgr(new QNetworkAccessManager(this))
{
    connect(m_netMgr, &QNetworkAccessManager::finished, this, &RequestAPI::onNetworkFinished);
    timeoutTimer.setInterval(2000); // 每 2s 检查一次超时
    connect(&timeoutTimer, &QTimer::timeout, this, &RequestAPI::checkPendingTimeouts);
    timeoutTimer.start();
    //test
    //requestForSlot("464876810296603");
    //uploadDropPackage("434413658764085");
}
RequestAPI::~RequestAPI()
{
    timeoutTimer.stop();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
    {
        QNetworkReply* r = it.key();
        if (r)
        {
            disconnect(r, nullptr, this, nullptr);
            r->abort();
            r->deleteLater();
        }
    }
    m_pending.clear();
}
void RequestAPI::enqueueOrSend(const QNetworkRequest& req, const QByteArray& payload, const QString& reqTag, int maxRetries, bool bypassPause)
{
    QMutexLocker l(&m_mutex);
    if (m_pending.size() >= concurrencyLimit)
    {
        if (m_requestQueue.size() < maxQueueSize)
        {
            m_requestQueue.enqueue(qMakePair(req, payload));
            log("---- [加入请求] 地址:[" + req.url().toString().toStdString() + "], 队列中数量:[" + std::to_string(m_requestQueue.size()) + "]");
        }
        else
        {
            log("---- [加入请求] 队列已满，正在丢弃请求: [" + req.url().toString().toStdString() + "]");
        }
    }
    else
    {
        QNetworkReply* reply = m_netMgr->post(req, payload);
        if (reply)
        {
            QVariantMap hmap;
            for (const QByteArray& hk : req.rawHeaderList())
            {
                hmap.insert(QString::fromUtf8(hk), QString::fromUtf8(req.rawHeader(hk)));
            }
            reply->setProperty("origHeaders", hmap);
            reply->setProperty("retriesLeft", maxRetries);
            reply->setProperty("reqTag", reqTag);
            reply->setProperty("payload", payload);
            reply->setProperty("reqUrl", req.url().toString());
            m_pending.insert(reply, QDateTime::currentMSecsSinceEpoch());
            log("---- [发送请求] 立即请求地址: [" + req.url().toString().toStdString() + "]");
        }
    }
}
QString RequestAPI::buildSignatureMd5(const QByteArray& payload) const
{
    auto timestamp = QString::number(currentTimeMillis());
    QString bodyStr;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error == QJsonParseError::NoError) {
        // JSON 有效 -> 使用紧凑表示（字段顺序仍由 JSON 本身决定）
        bodyStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    else {
        // 非 JSON，直接取 payload 的 UTF-8 字符串
        bodyStr = QString::fromUtf8(payload);
    }

    // 2) 生成 timestamp 字符串（毫秒或秒）

    // 3) 拼接原始串：appKey + bodyStr + timestamp + appSecret
    QString raw = _appkey + bodyStr + timestamp + _appSecret;

    // 4) 计算 MD5（UTF-8）
    QByteArray md5 = QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5);
    QString sign = QString(md5.toHex()); // 默认小写 hex

    //if (uppercase) sign = sign.toUpper();
    return sign;
}
void RequestAPI::checkPendingTimeouts()
{
    try
    {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<QNetworkReply*> toAbort;
        { // lock scope
            QMutexLocker l(&m_mutex);
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
                qint64 start = it.value();
                if (now - start > requestTimeoutMs) {
                    toAbort.append(it.key());
                }
            }
        }

        for (QNetworkReply* r : toAbort) {
            if (!r) continue;
            log("---- [请求中止] 正在中止已超时的请求接口: [" + r->property("reqUrl").toString().toStdString() + "]");
            r->abort();
        }
    }
    catch (const std::exception& e)
    {
        log("---- [检查超时] 请求超时异常: " + std::string(e.what()));
    }
}
void RequestAPI::tryStartNext()
{
    QMutexLocker l(&m_mutex);
    while (!m_requestQueue.isEmpty() && m_pending.size() < concurrencyLimit) {
        auto item = m_requestQueue.dequeue();
        QNetworkRequest req = item.first;

        QByteArray payload = item.second;
        QNetworkReply* reply = m_netMgr->post(req, payload);
        if (reply) {
            QVariantMap hmap;
            for (const QByteArray& hk : req.rawHeaderList())
            {
                hmap.insert(QString::fromUtf8(hk), QString::fromUtf8(req.rawHeader(hk)));
            }
            reply->setProperty("origHeaders", hmap);
            reply->setProperty("retriesLeft", /*合适的重试次数，比如*/ 3);
            reply->setProperty("reqTag", "queued");
            reply->setProperty("payload", payload);
            reply->setProperty("reqUrl", req.url().toString());
            m_pending.insert(reply, QDateTime::currentMSecsSinceEpoch());
            log("---- [发出请求] 已出列并发送请求: [" + req.url().toString().toStdString() + "]");
        }
    }
}
void RequestAPI::onNetworkFinished(QNetworkReply* reply) //所有的回调
{
    if (!reply) return;
    QString reqTag = reply->property("reqTag").toString();
    QString reqUrl = reply->property("reqUrl").toString();
    QNetworkReply::NetworkError netErr = reply->error();

    // 从 pending 移除（不论成功或失败，都先移除）
    {
        QMutexLocker l(&m_mutex);
        if (m_pending.contains(reply)) m_pending.remove(reply);
    }

    // ---------- 1) 网络错误 / abort 处理（先于读取数据）
    if (netErr != QNetworkReply::NoError)
    {
        log("---- [请求失败] 网络错误,接口地址[" + reqUrl.toStdString() + "],错误代码[" + std::to_string((int)netErr) + "],信息[" + reply->errorString().toStdString() + "]");
        int retriesLeft = reply->property("retriesLeft").toInt();
        if (retriesLeft > 0) {
            // 从 reply property 恢复原始 headers（如果有），以便重试时保持一致
            QVariantMap origHeaders = reply->property("origHeaders").toMap();
            QNetworkRequest newReq(reply->url());
            if (!origHeaders.isEmpty()) {
                for (auto it = origHeaders.begin(); it != origHeaders.end(); ++it) {
                    newReq.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
                }
            }

            QByteArray payload = reply->property("payload").toByteArray();
            reply->deleteLater();

            // 直接重发（注意设置 properties）
            QNetworkReply* newr = m_netMgr->post(newReq, payload);
            if (newr) {
                // 把原始 headers 也写回到新 reply（便于后续重试/保留信息）
                QVariantMap hmap;
                for (const QByteArray& hk : newReq.rawHeaderList()) {
                    hmap.insert(QString::fromUtf8(hk), QString::fromUtf8(newReq.rawHeader(hk)));
                }
                newr->setProperty("origHeaders", hmap);

                newr->setProperty("reqTag", reqTag);
                newr->setProperty("payload", payload);
                newr->setProperty("reqUrl", newReq.url().toString());
                newr->setProperty("retriesLeft", retriesLeft - 1);

                QMutexLocker l(&m_mutex);
                m_pending.insert(newr, QDateTime::currentMSecsSinceEpoch());
            }
            else {
                emit requestFailed(reqUrl, "重试请求失败!");
            }
        }
        else {
            emit requestFailed(reqUrl, reply->errorString());
            reply->deleteLater();
            tryStartNext();
        }
        return;
    }
    QByteArray data = reply->readAll();

    // ---------- 3) 解析 JSON
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        log("---- [数据返回] 响应不是JSON格式: 接口:[" + reqUrl.toStdString() + "], 报错信息[" + parseErr.errorString().toStdString() + "], 返回体:[" + QString::fromUtf8(data.left(512)).toStdString() + "]");
        emit requestFailed(reqUrl, "忽略JSON");
        reply->deleteLater();
        tryStartNext();
        return;
    }
    log("---- [数据返回] 接口:[" + reqUrl.toStdString() + "], 返回体:[" + QString::fromUtf8(data.left(512)).toStdString() + "], 标签:["+reqTag.toStdString() + "]");
    QJsonObject obj = doc.object();
    int code = obj.value("code").toInt(-1);
    QString msg = obj.value("message").toString();
    bool succ = obj.value("success").toBool();

    // ---------- 4) 处理 请求格口 逻辑
    try
    {
        if (reqTag == "get_slot")
        {
            if (code == 0 && succ)
            {
                if (obj.contains("data") && obj["data"].isObject())
                {
                    QJsonObject dataObj = obj["data"].toObject();
                    if (dataObj.contains("boxNo"))
                    {
                        QString slot_idStr = dataObj["boxNo"].toString();
                        int slot_id = std::stoi(slot_idStr.toStdString());
                        QString code = dataObj["sheetNo"].toString();
                        //log("---- [test] 获取格口成功: 单号:[" + code.toStdString() + "], 格口号:[" + slot_idStr.toStdString() + "], int 格口号: ["+std::to_string(slot_id)+"]");
                        emit getSlotSuccess(code, slot_id);
                        reply->deleteLater();
                        tryStartNext();
                        return;
                    }
                }
            }
            emit requestFailed(reqUrl, "获取格口: 无数据!");
            reply->deleteLater();
            tryStartNext();
            return;
        }
    }
    catch (const std::exception& e)
    {
        log("---- [错误] 处理格口返回异常: " + std::string(e.what()));
        emit requestFailed(reqUrl, "异常错误");
    }

    try
    {
        if (reqTag == "upload_drop_package")
        {
            if (code == 0 && succ)
            {
                //上传成功
                log("---- [集包返回] 集包成功!");
                reply->deleteLater();
                tryStartNext();
                return;
            }
            else
            {
                log("---- [集包返回] 集包失败!");
            }
            reply->deleteLater();
            tryStartNext();
            return;
        }
    }
    catch (const std::exception& e)
    {
        log("---- [错误] 集包返回处理异常: " + std::string(e.what()));
        emit requestFailed(reqUrl, "异常错误");
    }

    reply->deleteLater();
    tryStartNext();
}

//API: 获取格口 落格集包
void RequestAPI::requestForSlot(const QString& code)
{
    QJsonObject body;
    body["sheetNo"] = code;
    body["segmentCode"] = "";
    body["needUpload"] = true;
    body["isFstCode"] = false;
    body["weight"] = 1;
    QJsonDocument doc(body);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    log("---- [格口请求] 请求体: " + QString::fromUtf8(payload).toStdString());

    QUrl url(_getSlotUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("timestamp", QString::number(currentTimeMillis()).toUtf8());
    req.setRawHeader("appid", _appkey.toUtf8());
    req.setRawHeader("sign", buildSignatureMd5(payload).toUtf8());
    enqueueOrSend(req, payload, "get_slot", 3);
}

void RequestAPI::uploadDropPackage(const QString& code)
{
    QJsonObject body;
    body["sheetNo"] = code;
    QJsonDocument doc(body);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);
    log("---- [集包请求] 请求体: " + QString::fromUtf8(payload).toStdString());

    QUrl url(_uploadDropPackageUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("timestamp", QString::number(currentTimeMillis()).toUtf8());
    req.setRawHeader("appid", _appkey.toUtf8());
    req.setRawHeader("sign", buildSignatureMd5(payload).toUtf8());
    enqueueOrSend(req, payload, "upload_drop_package", 3);
}

