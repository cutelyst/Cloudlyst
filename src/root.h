#ifndef ROOT_H
#define ROOT_H

#include <Cutelyst/Controller>

using namespace Cutelyst;

class Root : public Controller
{
    Q_OBJECT
    C_NAMESPACE("")
public:
    explicit Root(QObject *parent = 0);
    ~Root();

    C_ATTR(index, :Path :AutoArgs)
    void index(Context *c);

    C_ATTR(defaultPage, :Path)
    void defaultPage(Context *c);

    // Hackery to work with NextCloud-client
    C_ATTR(statusPhp, :Path('status.php') :AutoArgs)
    void statusPhp(Context *c);

    // Hackery to work with NextCloud-client
    C_ATTR(remoteDanielPhp, :Path('remote.php/dav/files/daniel') :AutoArgs)
    void remoteDanielPhp(Context *c, const QStringList &pathParts);

    C_ATTR(remotePhp, :Path('remote.php/webdav') :AutoArgs)
    void remotePhp(Context *c, const QStringList &pathParts);

    C_ATTR(configPhp, :Path('ocs/v1.php/config') :AutoArgs)
    void configPhp(Context *c);

    C_ATTR(capabilitiesPhp, :Path('ocs/v1.php/cloud/capabilities') :AutoArgs)
    void capabilitiesPhp(Context *c);

private:
//    C_ATTR(End, :ActionClass("RenderView"))
//    void End(Context *c) { Q_UNUSED(c); }
};

#endif //ROOT_H

