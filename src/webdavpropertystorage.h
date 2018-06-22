#ifndef WEBDAVPROPERTYSTORAGE_H
#define WEBDAVPROPERTYSTORAGE_H

#include <QObject>
#include <QHash>

typedef QHash<QString, QString> PropertyValueHash;
typedef QHash<qint64, PropertyValueHash> PathPropertyHash;

class WebdavPropertyStorage : public QObject
{
    Q_OBJECT
public:
    explicit WebdavPropertyStorage(QObject *parent = nullptr);

    static inline QString propertyKey(const QStringRef &name, const QStringRef &ns) {
        return QLatin1Char('{') + ns + QLatin1Char('}') + name;
    }

    static inline QString propertyKey(const QString &name, const QString &ns) {
        return QLatin1Char('{') + ns + QLatin1Char('}') + name;
    }

    virtual bool begin();

    virtual bool commit();

    virtual bool rollback();

    virtual bool setValue(qint64 file_id, const QString &key, const QString &value);

    virtual bool remove(qint64 file_id, const QString &key);

private:
    PathPropertyHash m_pathProps;
    PathPropertyHash m_pathPropsTransaction;
    bool m_transaction = false;
};

#endif // WEBDAVPROPERTYSTORAGE_H
