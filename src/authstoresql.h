#ifndef AUTHSTORESQL_H
#define AUTHSTORESQL_H

#include <Cutelyst/Plugins/Authentication/authenticationstore.h>

#include <QObject>

using namespace Cutelyst;

class AuthStoreSql : public AuthenticationStore
{
    Q_OBJECT
public:
    explicit AuthStoreSql(QObject *parent = nullptr);

    virtual AuthenticationUser findUser(Context *c, const ParamsMultiMap &userinfo) override final;
};

#endif // AUTHSTORESQL_H
