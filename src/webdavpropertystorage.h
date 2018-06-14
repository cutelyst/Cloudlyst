#ifndef WEBDAVPROPERTYSTORAGE_H
#define WEBDAVPROPERTYSTORAGE_H

#include <QObject>
#include <QHash>

typedef QHash<QString, QString> PropertyValueHash;
typedef QHash<QString, PropertyValueHash> PathPropertyHash;

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

    virtual bool setValue(const QString &path, const QString &key, const QString &value);

    virtual bool remove(const QString &path, const QString &key);

    virtual bool moveValues(const QString &path, const QString &newPath);

    virtual QString value(const QString &path, const QString &key, bool &found) const;

private:
    PathPropertyHash m_pathProps;
    PathPropertyHash m_pathPropsTransaction;
    bool m_transaction = false;
};

#endif // WEBDAVPROPERTYSTORAGE_H
