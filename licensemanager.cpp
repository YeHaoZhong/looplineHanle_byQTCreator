#include "licensemanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QBoxLayout>
#include <QMessageBox>
#include <QDateTime>
#include <sodium.h>

static QByteArray base64url_to_base64(const QString& in) {
    QByteArray b = in.toUtf8();
    b.replace('-', '+');
    b.replace('_', '/');
    int mod = b.size() % 4;
    if (mod) b.append(4 - mod, '=');
    return b;
}

LicenseManager::LicenseManager(const QString& path) : m_path(path) {
    if (sodium_init() < 0) {
        // fatal: libsodium init failed
    }
}
bool LicenseManager::load() {
    QFile f(m_path);
    if (!f.exists()) {
        // ---------- 固定首次使用时间（在代码中写死） ----------
        // 把下面的 YEAR/MONTH/DAY 改成你想要“首次使用”的日期（例如 2025-12-08）
        const QDate FIRST_RUN_DATE(2025, 12, 22);
        const QTime FIRST_RUN_TIME(0, 0, 0); // 00:00:00
        // 把 epoch 当作 UTC 来处理（更确定），然后在显示时用 toLocalTime()
        QDateTime firstRunDT(FIRST_RUN_DATE, FIRST_RUN_TIME, QTimeZone::utc());

        qint64 firstRunEpoch = firstRunDT.toSecsSinceEpoch();

        // 这里仍然保留 7 天试用期（你可以改成 30 天或其它）
        const qint64 TRIAL_SECONDS = 7 * 24 * 3600LL;

        m_expiry = firstRunEpoch + TRIAL_SECONDS;
        m_lastRun = firstRunEpoch;
        m_usedNonces.clear();
        return save();
    }
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray data = f.readAll();
    f.close();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) return false;
    QJsonObject o = doc.object();
    m_expiry = qint64(o.value("expiry").toVariant().toLongLong());
    m_lastRun = qint64(o.value("last_run").toVariant().toLongLong());
    m_usedNonces.clear();
    QJsonArray arr = o.value("used_nonces").toArray();
    for (auto v : arr) m_usedNonces.insert(v.toString());
    return true;
}

bool LicenseManager::save() {
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QJsonObject o;
    o["expiry"] = QJsonValue::fromVariant(QVariant::fromValue(qint64(m_expiry)));
    o["last_run"] = QJsonValue::fromVariant(QVariant::fromValue(qint64(m_lastRun)));
    QJsonArray arr;
    int count = 0;
    for (auto it = m_usedNonces.begin(); it != m_usedNonces.end() && count < 200; ++it, ++count)
        arr.append(*it);
    o["used_nonces"] = arr;
    QJsonDocument doc(o);
    f.write(doc.toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

QDateTime LicenseManager::expiryDateTime() const {
    return QDateTime::fromSecsSinceEpoch(m_expiry).toLocalTime();
}

bool LicenseManager::isExpired() const {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    return now > m_expiry;
}

QString LicenseManager::getMachineId() const {
    // 使用 hostname + 第一个物理 MAC 地址的 SHA256 摘要作为 machine id
    QString host = QHostInfo::localHostName();
    QByteArray raw = host.toUtf8();
    QByteArray hash = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
    return QString(hash.toHex());
}

void LicenseManager::trimUsedNonces() {
    // 当 used_nonces 超长时，保留最新 N 项（这里简单不作时间戳，按任意）
    while (m_usedNonces.size() > 200) {
        auto it = m_usedNonces.begin();
        m_usedNonces.erase(it);
    }
}

bool LicenseManager::verifyTokenAndApply(const QString& token, QString& errOut) {
    // token 格式: base64url(payload) . base64url(signature)
    int dot = token.indexOf('.');
    if (dot < 0) { errOut = "Token format invalid"; return false; }
    QString payload_b64 = token.left(dot);
    QString sig_b64 = token.mid(dot + 1);

    QByteArray payload = QByteArray::fromBase64(base64url_to_base64(payload_b64));
    QByteArray signature = QByteArray::fromBase64(base64url_to_base64(sig_b64));

    if (payload.isEmpty() || signature.isEmpty()) { errOut = "Token decode failed"; return false; }
    if (signature.size() != crypto_sign_BYTES) { errOut = "Signature length invalid"; return false; }

    // 内置公钥（hex），请在构建项目时将管理员生成的公钥 hex 放在下面
    const char* PUBHEX = "7d2cce2b6c8abe3b4d8b94d0a1eb1e29a96cb314e40ea8fe478f6a383bc45f70"; // 64 hex chars -> 32 bytes
    unsigned char pk[32];
    if (strlen(PUBHEX) != 64) {  errOut = "Public key not set"; return false; }
    unsigned int b;
    for (int i = 0; i < 32; ++i) {
        // 使用 sscanf_s 替代 sscanf（MSVC）
        if (sscanf_s(PUBHEX + i * 2, "%02x", &b) != 1) {
            errOut = "Public key parse failed";
            return false;
        }
        pk[i] = static_cast<unsigned char>(b);
    }


    // 验签：libsodium expects unsigned char*
    if (crypto_sign_verify_detached((const unsigned char*)signature.constData(),
                                    (const unsigned char*)payload.constData(),
                                    (unsigned long long)payload.size(),
                                    pk) != 0) {
        errOut = "Signature verification failed";
        return false;
    }

    // 解析 payload JSON (payload 是紧凑 JSON)
    QJsonParseError jerr;
    QJsonDocument jdoc = QJsonDocument::fromJson(payload, &jerr);
    if (jerr.error != QJsonParseError::NoError) { errOut = "Payload JSON parse failed"; return false; }
    QJsonObject o = jdoc.object();
    QString mid = o.value("mid").toString();
    qint64 exp = qint64(o.value("exp").toVariant().toLongLong());
    QString nonce = o.value("nonce").toString();

    if (mid.isEmpty() || exp <= 0 || nonce.isEmpty()) { errOut = "Payload missing fields"; return false; }

    QString mymid = getMachineId();
    if (mymid != mid) { errOut = "Machine ID mismatch"; return false; }

    if (m_usedNonces.contains(nonce)) { errOut = "Nonce already used (replay)"; return false; }

    // 确保管理员生成的 exp 合理：必须大于当前 expiry
    if (exp <= m_expiry) { errOut = "Token expiry not newer than current expiry"; return false; }

    // 通过所有检查：应用新的到期时间 & 记录 nonce & 更新 last_run
    m_expiry = exp;
    m_lastRun = QDateTime::currentSecsSinceEpoch();
    m_usedNonces.insert(nonce);
    trimUsedNonces();
    if (!save()) { errOut = "Save license failed"; return false; }
    return true;
}
qint64 LicenseManager::addOneMonthToEpoch(qint64 epoch) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::utc());
    dt = dt.addMonths(1);
    return dt.toSecsSinceEpoch();
}

bool LicenseManager::checkAndPrompt(QWidget* parent) {
    if (!load()) {
        QMessageBox::critical(parent, "License", "许可验证失败");
        return false;
    }
    qint64 now = QDateTime::currentSecsSinceEpoch();
    // 时间回拨检测
    if (now < m_lastRun) {
        QMessageBox::critical(parent, "License", QString("检测到系统时间被回拨，上次运行时间: %1").arg(QDateTime::fromSecsSinceEpoch(m_lastRun).toString()));
        return false;
    }

    // 创建一个简单对话框
    QDialog dlg(parent);
    dlg.setWindowTitle("软件授权");
    QVBoxLayout* vl = new QVBoxLayout(&dlg);
    QLabel* label = new QLabel(QString("当前授权截至： %1").arg(expiryDateTime().toString()));
    vl->addWidget(label);
    QLabel* hint = new QLabel("（到期后需要输入授权码以续期）");
    vl->addWidget(hint);
    QLineEdit* edit = new QLineEdit();
    edit->setPlaceholderText("请输入授权码，到期才能输入有效）");
    vl->addWidget(edit);
    QHBoxLayout* hl = new QHBoxLayout();
    QPushButton* ok = new QPushButton("确定");
    QPushButton* cancel = new QPushButton("退出");
    hl->addStretch();
    hl->addWidget(ok);
    hl->addWidget(cancel);
    vl->addLayout(hl);

    QObject::connect(ok, &QPushButton::clicked, &dlg, [&]() {
        QString token = edit->text().trimmed();

        if (!isExpired()) {
            if (!token.isEmpty()) {
                // 尝试验证授权码
                QString err;
                if (!verifyTokenAndApply(token, err)) {
                    QMessageBox::warning(&dlg, "授权失败", err);
                    return;
                }
                // 授权码有效，延长当前到期日期一个月
                //m_expiry = addOneMonthToEpoch(m_expiry);
                QMessageBox::information(&dlg, "授权成功",
                                         QString("新的到期时间：%1").arg(expiryDateTime().toString()));
            }
            // 无论是否输入授权码，都更新 last_run
            m_lastRun = QDateTime::currentSecsSinceEpoch();
            save();
            dlg.accept();
            return;
        }

        // 已过期，必须输入授权码
        if (token.isEmpty()) {
            QMessageBox::warning(&dlg, "授权", "已过期，请输入授权码。");
            return;
        }
        QString err;
        if (!verifyTokenAndApply(token, err)) {
            QMessageBox::warning(&dlg, "授权失败", err);
            return;
        }

        // 已过期且授权码有效 → 设置到期时间为当前时间 + 一个月
        //m_expiry = addOneMonthToEpoch(QDateTime::currentSecsSinceEpoch());
        QMessageBox::information(&dlg, "授权成功",
                                 QString("新的到期时间：%1").arg(expiryDateTime().toString()));
        m_lastRun = QDateTime::currentSecsSinceEpoch();
        save();
        dlg.accept();
    });

    QObject::connect(cancel, &QPushButton::clicked, &dlg, [&]() {
        dlg.reject();
    });
    int ret = dlg.exec();
    return ret == QDialog::Accepted;
}
