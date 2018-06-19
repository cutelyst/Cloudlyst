#include "authstoresql.h"

#include <Cutelyst/Plugins/Utils/Sql>
#include <Cutelyst/Context>

#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QDebug>

AuthStoreSql::AuthStoreSql(QObject *parent) : AuthenticationStore(parent)
{

}

AuthenticationUser AuthStoreSql::findUser(Context *c, const ParamsMultiMap &userinfo)
{
    Q_UNUSED(c)
    AuthenticationUser ret;
    const QString username = userinfo.value(QStringLiteral("username"));

    QSqlQuery findUserQuery = CPreparedSqlQueryThreadForDB(QStringLiteral("SELECT id, username, displayname, password "
                                                                     "FROM cloudlyst.users "
                                                                     "WHERE username = :username"),
                                                           QStringLiteral("cloudlyst"));

    findUserQuery.bindValue(QStringLiteral(":username"), username);
    qDebug() << findUserQuery.executedQuery() << username;

    if (findUserQuery.exec() && findUserQuery.next()) {
        const QVariant userId = findUserQuery.value(QStringLiteral("id"));
        qDebug() << "FOUND USER -> " << userId;
        ret.setId(userId);

        int colunas = findUserQuery.record().count();
        // send column headers
        QStringList cols;
        for (int j = 0; j < colunas; ++j) {
            cols << findUserQuery.record().fieldName(j);
        }

        for (int j = 0; j < colunas; ++j) {
            ret.insert(cols.at(j),
                        findUserQuery.value(j).toString());
        }
        qDebug() << "user.roles" << ret;

//        QSqlQuery findUserRolesQuery = CPreparedSqlQuery(
//                    QStringLiteral("SELECT distinct(r.role) "
//                                   "FROM aaa.roles r "
//                                   "INNER JOIN aaa.group_role gr "
//                                   "ON r.id = gr.role_id "
//                                   "INNER JOIN aaa.user_group ug "
//                                   "ON gr.group_id = ug.group_id AND ug.user_id = :user_id"));

//        findUserRolesQuery.bindValue(QStringLiteral(":user_id"), userId);
//        if (findUserRolesQuery.exec()) {
//            while (findUserRolesQuery.next()) {
//                ret.insertMulti(QStringLiteral("roles"), findUserRolesQuery.value(0).toString());
//            }
//            qDebug() << "user.roles" << user.values("roles");
//        }

        return ret;
    }
    qDebug() << findUserQuery.lastError().text();

    return ret;
}
