#include "webdavpropertystorage.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(WEBDAV_STORAGE, "webdav.storage")

WebdavPropertyStorage::WebdavPropertyStorage(QObject *parent) : QObject(parent)
{

}

bool WebdavPropertyStorage::begin()
{
    if (!m_transaction) {
        m_transaction = true;
        m_pathPropsTransaction = m_pathProps;
    }
    return true;
}

bool WebdavPropertyStorage::commit()
{
    if (m_transaction) {
        m_transaction = false;
        m_pathProps = m_pathPropsTransaction;
    }
    return true;
}

bool WebdavPropertyStorage::rollback()
{
    m_transaction = false;
    return true;
}

bool WebdavPropertyStorage::setValue(const QString &path, const QString &key, const QString &value)
{
    begin();

    qCDebug(WEBDAV_STORAGE) << "SET" << path << key << value;
    m_pathPropsTransaction[path][key] = value;

    return true;
}

bool WebdavPropertyStorage::remove(const QString &path, const QString &key)
{
    begin();
    qCDebug(WEBDAV_STORAGE) << "REMOVE" << path << key;

    auto pathIt = m_pathPropsTransaction.find(path);
    if (pathIt == m_pathPropsTransaction.end()) {
        return true;
    }

    PropertyValueHash &propValues = pathIt.value();

    const auto propIt = propValues.constFind(key);
    if (propIt != propValues.constEnd()) {
        propValues.erase(propIt);
    }

    if (propValues.empty()) {
        m_pathPropsTransaction.erase(pathIt);
    }

    return true;
}

bool WebdavPropertyStorage::moveValues(const QString &path, const QString &newPath)
{
    begin();
    qCDebug(WEBDAV_STORAGE) << "MOVE" << path << newPath;

    auto pathIt = m_pathPropsTransaction.constFind(path);
    if (pathIt != m_pathPropsTransaction.constEnd()) {
        const PropertyValueHash propValues = pathIt.value();
        m_pathPropsTransaction.erase(pathIt);
        m_pathPropsTransaction.insert(newPath, propValues);
    }
    return true;
}

QString WebdavPropertyStorage::value(const QString &path, const QString &key, bool &found) const
{
    QString ret;

    const PathPropertyHash pathProps = m_transaction ? m_pathPropsTransaction : m_pathProps;

    const auto pathIt = pathProps.constFind(path);
    if (pathIt != pathProps.constEnd()) {
        const PropertyValueHash &propValues = pathIt.value();
        const auto propIt = propValues.constFind(key);
        if (propIt != propValues.constEnd()) {
            found = true;
            ret = propIt.value();
            qCDebug(WEBDAV_STORAGE) << "GET" << path << key << ret;
            return ret;
        }
    }
    found = false;
    qCDebug(WEBDAV_STORAGE) << "GET NOT FOUND" << path << key;

    return ret;
}
