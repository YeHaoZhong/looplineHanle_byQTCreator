#ifndef LICENSEMANAGER_H
#define LICENSEMANAGER_H

#include <QString>
#include <QDateTime>
#include <QSet>

class QWidget;
class QDialog;
class LicenseManager {
public:
    LicenseManager(const QString& path = "license.json");
    bool load();
    bool save();
    qint64 expiryEpoch() const { return m_expiry; }
    QDateTime expiryDateTime() const;
    bool isExpired() const;
    bool checkAndPrompt(QWidget* parent); // 主接口：在 main() 里调用，弹窗并执行可交互的授权逻辑

private:
    bool verifyTokenAndApply(const QString& token, QString& errOut);
    QString getMachineId() const;
    void trimUsedNonces();
    qint64 addOneMonthToEpoch(qint64 epoch);

private:
    QString m_path;
    qint64 m_expiry = 0;      // epoch seconds
    qint64 m_lastRun = 0;
    QSet<QString> m_usedNonces;
};

#endif // LICENSEMANAGER_H
