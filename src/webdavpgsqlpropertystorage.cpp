#include "webdavpgsqlpropertystorage.h"

#include <Cutelyst/Plugins/Utils/Sql>

#include <QSqlQuery>
#include <QSqlError>

using namespace Cutelyst;

WebdavPgSqlPropertyStorage::WebdavPgSqlPropertyStorage(QObject *parent) : WebdavPropertyStorage(parent)
{

}

bool WebdavPgSqlPropertyStorage::begin()
{
    QSqlDatabase db = Sql::databaseThread(QStringLiteral("cloudlyst"));
    return db.transaction();
}

bool WebdavPgSqlPropertyStorage::commit()
{
    QSqlDatabase db = Sql::databaseThread(QStringLiteral("cloudlyst"));
    return db.commit();
}

bool WebdavPgSqlPropertyStorage::rollback()
{
    QSqlDatabase db = Sql::databaseThread(QStringLiteral("cloudlyst"));
    return db.rollback();
}

bool WebdavPgSqlPropertyStorage::setValue(qint64 file_id, const QString &key, const QString &value)
{
    QSqlQuery query = CPreparedSqlQueryThreadForDB(QStringLiteral("INSERT INTO cloudlyst.file_properties "
                                                                  "(file_id, name, value) "
                                                                  "VALUES "
                                                                  "(:file_id, :name, :value) "
                                                                  "ON CONFLICT ON CONSTRAINT file_properties_file_id_name_key "
                                                                  "DO UPDATE SET value = :updatevalue"),
                                                   QStringLiteral("cloudlyst"));
    query.bindValue(QStringLiteral(":file_id"), file_id);
    query.bindValue(QStringLiteral(":name"), key);
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":updatevalue"), value);
    if (query.exec() && query.numRowsAffected()) {
        return true;
    }

    return false;
}

bool WebdavPgSqlPropertyStorage::remove(qint64 file_id, const QString &key)
{
    QSqlQuery query = CPreparedSqlQueryThreadForDB(QStringLiteral("DELETE FROM cloudlyst.file_properties "
                                                                  "WHERE file_id = :file_id AND name = :name"),
                                                   QStringLiteral("cloudlyst"));
    query.bindValue(QStringLiteral(":file_id"), file_id);
    query.bindValue(QStringLiteral(":name"), key);
    if (query.exec() && query.numRowsAffected()) {
        return true;
    }

    return false;
}
