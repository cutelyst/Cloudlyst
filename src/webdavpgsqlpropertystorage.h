#ifndef WEBDAVPGSQLPROPERTYSTORAGE_H
#define WEBDAVPGSQLPROPERTYSTORAGE_H

#include <QObject>

#include "webdavpropertystorage.h"

class WebdavPgSqlPropertyStorage : public WebdavPropertyStorage
{
    Q_OBJECT
public:
    explicit WebdavPgSqlPropertyStorage(QObject *parent = nullptr);

    virtual bool begin() override final;

    virtual bool commit() override final;

    virtual bool rollback() override final;

    virtual bool setValue(qint64 file_id, const QString &key, const QString &value) override final;

    virtual bool remove(qint64 file_id, const QString &key) override final;
};

#endif // WEBDAVPGSQLPROPERTYSTORAGE_H
