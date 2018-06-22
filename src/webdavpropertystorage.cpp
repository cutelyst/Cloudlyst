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

bool WebdavPropertyStorage::setValue(qint64 file_id, const QString &key, const QString &value)
{
    begin();

    qCDebug(WEBDAV_STORAGE) << "SET" << file_id << key << value;
    m_pathPropsTransaction[file_id][key] = value;

    return true;
}

bool WebdavPropertyStorage::remove(qint64 file_id, const QString &key)
{
    begin();
    qCDebug(WEBDAV_STORAGE) << "REMOVE" << file_id << key;

    auto pathIt = m_pathPropsTransaction.find(file_id);
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
