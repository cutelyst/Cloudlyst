#include "root.h"

#include <QJsonObject>
#include <QJsonArray>

#include <QLoggingCategory>

using namespace Cutelyst;

Root::Root(QObject *parent) : Controller(parent)
{
}

Root::~Root()
{
}

void Root::index(Context *c)
{
    c->response()->body() = "Welcome to Cutelyst!";
}

void Root::defaultPage(Context *c)
{
    c->response()->body() = "Page not found!";
    c->response()->setStatus(404);
}

void Root::statusPhp(Context *c)
{
    c->response()->setJsonObjectBody({
                                         {QStringLiteral("version"), QStringLiteral("14.0.0.1")},
                                         {QStringLiteral("productname"), QStringLiteral("Nextcloud")},
                                         {QStringLiteral("versionstring"), QStringLiteral("14.0.0.1 alpha")},
                                         {QStringLiteral("installed"), true},
                                         {QStringLiteral("maintenance"), false},
                                         {QStringLiteral("needsDbUpgrade"), false},
                                     });
}

void Root::remoteDavPhp(Context *c, const QStringList &pathParts)
{
    Q_UNUSED(pathParts)
    qDebug() << c->request()->match() << pathParts;
    const QStringList argsWithoutUser = pathParts.mid(1);
    c->request()->setArguments(argsWithoutUser);

    QString match = c->request()->match();
    int pos = match.indexOf(QLatin1Char('/'));
    if (pos == -1) {
        c->request()->setMatch(QString());
    } else {
        c->request()->setMatch(match.mid(pos + 1));
    }
    c->forward(QStringLiteral("/webdav/dav"));
}

void Root::remotePhp(Context *c, const QStringList &pathParts)
{
    Q_UNUSED(pathParts)
    c->forward(QStringLiteral("/webdav/dav"));
}

void Root::configPhp(Context *c)
{
    //{"ocs":{"meta":{"status":"ok","statuscode":100,"message":"OK","totalitems":"","itemsperpage":""},
    // "data":{"version":"1.7","website":"Nextcloud","host":"localhost","contact":"","ssl":"false"}}}
    QJsonObject meta{
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("statuscode"), 100},
        {QStringLiteral("message"), QStringLiteral("OK")},
        {QStringLiteral("totalitems"), QStringLiteral("")},
        {QStringLiteral("itemsperpage"), QStringLiteral("")},
    };
    QJsonObject data{
        {QStringLiteral("version"), QStringLiteral("1.7")},
        {QStringLiteral("website"), QStringLiteral("Nextcloud")},
        {QStringLiteral("host"), QStringLiteral("localhost")},
        {QStringLiteral("contact"), QStringLiteral("")},
        {QStringLiteral("ssl"), false},
    };
    QJsonObject ocs{
        {QStringLiteral("meta"), meta},
        {QStringLiteral("data"), data},
    };
    c->response()->setJsonObjectBody({
                                         {QStringLiteral("ocs"), ocs},
                                     });
}

void Root::capabilitiesPhp(Context *c)
{
    //{"ocs":{"meta":{"status":"ok","statuscode":100,"message":"OK","totalitems":"","itemsperpage":""},
    //"data":{"version":{"major":14,"minor":0,"micro":0,"string":"14.0.0 alpha","edition":""},
    //"capabilities":{
    //"core":{"pollinterval":60,"webdav-root":"remote.php\/webdav"},
    //"bruteforce":{"delay":0},
    //"dav":{"chunking":"1.0"},
    //"end-to-end-encryption":{"enabled":true,"api-version":"1.0"},
    //"files_sharing":{"api_enabled":true,"public":{"enabled":true,"password":{"enforced":false},"expire_date":{"enabled":false},"send_mail":false,"upload":true,"upload_files_drop":true},"resharing":true,"user":{"send_mail":false,"expire_date":{"enabled":true}},"group_sharing":true,"group":{"enabled":true,"expire_date":{"enabled":true}},"default_permissions":31,"federation":{"outgoing":true,"incoming":true,"expire_date":{"enabled":true}},"sharebymail":{"enabled":true,"upload_files_drop":{"enabled":true},"password":{"enabled":true},"expire_date":{"enabled":true}}},"theming":{"name":"Nextcloud","url":"https:\/\/nextcloud.com","slogan":"Um lar seguro para todos os seus dados","color":"#0082c9","color-text":"#ffffff","color-element":"#0082c9","logo":"http:\/\/localhost\/nextcloud\/core\/img\/logo.svg?v=0","background":"http:\/\/localhost\/nextcloud\/core\/img\/background.png?v=0","background-plain":false,"background-default":true},"files":{"bigfilechunking":true,"blacklisted_files":[".htaccess"],"undelete":true,"versioning":true}}}}}

    QJsonObject meta{
        {QStringLiteral("status"), QStringLiteral("ok")},
        {QStringLiteral("statuscode"), 100},
        {QStringLiteral("message"), QStringLiteral("OK")},
        {QStringLiteral("totalitems"), QStringLiteral("")},
        {QStringLiteral("itemsperpage"), QStringLiteral("")},
    };
    QJsonObject data{
        {QStringLiteral("version"), QJsonObject{
                {QStringLiteral("major"), QStringLiteral("14")},
                {QStringLiteral("minor"), QStringLiteral("0")},
                {QStringLiteral("micro"), QStringLiteral("0")},
                {QStringLiteral("edition"), QStringLiteral("")},
                {QStringLiteral("string"), QStringLiteral("14.0.0 alpha")},
            }},
        {QStringLiteral("capabilities"), QJsonObject{
                {QStringLiteral("core"), QJsonObject{
                        {QStringLiteral("pollinterval"), 60},
                        {QStringLiteral("webdav-root"), QStringLiteral("remote.php\\/webdav")},
                    }},
                {QStringLiteral("bruteforce"), QJsonObject{
                        {QStringLiteral("delay"), 0},
                    }},
                {QStringLiteral("dav"), QJsonObject{
                        {QStringLiteral("chunking"), "1.0"},
                    }},
                {QStringLiteral("end-to-end-encryption"), QJsonObject{
                        {QStringLiteral("enabled"), false},
                        {QStringLiteral("api-version"), QStringLiteral("1.0")},
                    }},
                {QStringLiteral("files"), QJsonObject{
                        {QStringLiteral("bigfilechunking"), true},
                        {QStringLiteral("undelete"), true},
                        {QStringLiteral("versioning"), true},
                        {QStringLiteral("blacklisted_files"), QJsonArray{
                                QStringLiteral(".htaccess")
                            }},
                    }},
            }},
    };
    QJsonObject ocs{
        {QStringLiteral("meta"), meta},
        {QStringLiteral("data"), data},
    };
    c->response()->setJsonObjectBody({
                                         {QStringLiteral("ocs"), ocs},
                                     });
}

