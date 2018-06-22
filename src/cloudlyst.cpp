#include "cloudlyst.h"

#include <QCoreApplication>
#include <QMutexLocker>

#include <Cutelyst/Plugins/Authentication/credentialhttp.h>
#include <Cutelyst/Plugins/Authentication/minimal.h>
#include <Cutelyst/Plugins/Session/Session>
#include <Cutelyst/Plugins/Utils/Sql>

#include "authstoresql.h"

#include "root.h"
#include "webdav.h"

#include <QSqlQuery>
#include <QSqlError>

#include <QLoggingCategory>

using namespace Cutelyst;

static QMutex dbMutex;

Cloudlyst::Cloudlyst(QObject *parent) : Application(parent)
{
    QCoreApplication::setApplicationName(QStringLiteral("Cloudlyst"));
}

Cloudlyst::~Cloudlyst()
{
}

bool Cloudlyst::init()
{
    new Root(this);
    new Webdav(this);

    auto httpCred = new CredentialHttp;
    httpCred->setPasswordType(CredentialHttp::None);
    httpCred->setUsernameField(QStringLiteral("username"));

    auto store = new AuthStoreSql;

    auto auth = new Authentication(this);
    auth->addRealm(store, httpCred, QStringLiteral("Cloudlyst"));

    new Session(this);

    return true;
}

bool Cloudlyst::postFork()
{
    QMutexLocker locker(&dbMutex);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QPSQL"), Sql::databaseNameThread(QStringLiteral("cloudlyst")));
    db.setDatabaseName(QStringLiteral("cloudlyst"));
    if (!db.open()) {
        qCritical() << "Failed to open db" << db.lastError().databaseText();
        return false;
    }

    return createDB();
}

bool Cloudlyst::createDB()
{
    QSqlDatabase db = Sql::databaseThread(QStringLiteral("cloudlyst"));
    const QStringList tables = db.tables();
    qDebug() << "tables" << tables;

    QSqlQuery query(db);

    if (!tables.contains(QLatin1String("cloudlyst.users")) &&
            !query.exec(QStringLiteral("CREATE TABLE cloudlyst.users "
                                       "( id SERIAL PRIMARY KEY"
                                       ", username character varying(64) UNIQUE NOT NULL"
                                       ", displayname character varying(64)"
                                       ", password character varying(255) NOT NULL"
                                       ");"))) {
        qDebug() << "error" << query.lastError().databaseText();
        return false;
    }

    if (!tables.contains(QLatin1String("cloudlyst.mimetypes")) &&
            !query.exec(QStringLiteral("CREATE TABLE cloudlyst.mimetypes "
                                       "( id SERIAL PRIMARY KEY"
                                       ", name character varying(155) UNIQUE NOT NULL"
                                       ")"))) {
        qDebug() << "error" << query.lastError().databaseText();
        return false;
    }

    if (!tables.contains(QLatin1String("cloudlyst.files")) &&
            !query.exec(QStringLiteral("CREATE TABLE cloudlyst.files "
                                       "( id BIGSERIAL PRIMARY KEY"
                                       ", parent_id bigint REFERENCES cloudlyst.files(id) ON DELETE CASCADE"
                                       ", owner_id integer REFERENCES cloudlyst.users(id) NOT NULL"
                                       ", path character varying NOT NULL"
                                       ", name character varying"
                                       ", mimetype_id integer REFERENCES cloudlyst.mimetypes(id)"
                                       ", mtime integer NOT NULL "
                                       ", size bigint NOT NULL"
                                       ", etag character varying(40) NOT NULL"
                                       ", UNIQUE(path, owner_id)"
                                       ") "))) {
        qDebug() << "error" << query.lastError().databaseText();
        return false;
    }

    if (!tables.contains(QLatin1String("cloudlyst.file_properties")) &&
            !query.exec(QStringLiteral("CREATE TABLE cloudlyst.file_properties "
                                       "( id SERIAL PRIMARY KEY"
                                       ", file_id bigint REFERENCES cloudlyst.files(id) ON DELETE CASCADE NOT NULL "
                                       ", name character varying(255) NOT NULL"
                                       ", value character varying NOT NULL"
                                       ", UNIQUE(file_id, name)"
                                       ");"))) {
        qDebug() << "error" << query.lastError().databaseText();
        return false;
    }

    return true;
}
