#ifndef WEBDAV_H
#define WEBDAV_H

#include <Cutelyst/Controller>

#include <QMimeDatabase>

using namespace Cutelyst;

typedef QHash<QString, QString> Properties;

class QFileInfo;
class QXmlStreamReader;
class QXmlStreamWriter;
class Webdav : public Controller
{
    Q_OBJECT
public:
    explicit Webdav(QObject *parent = 0);
    ~Webdav();

    enum Prop {
        Resourcetype = 0x1,
        Getcontenttype = 0x2,
        Getetag = 0x4,
    };
    Q_DECLARE_FLAGS(Props, Prop)
    Q_FLAG(Props)

    C_ATTR(dav, :Path :AutoArgs :ActionClass(REST))
    void dav(Context *c, const QStringList &pathParts);

    C_ATTR(dav_HEAD, :Private)
    void dav_HEAD(Context *c, const QStringList &pathParts);

    C_ATTR(dav_GET, :Private)
    void dav_GET(Context *c, const QStringList &pathParts);

    C_ATTR(dav_DELETE, :Private)
    void dav_DELETE(Context *c, const QStringList &pathParts);

    C_ATTR(dav_COPY, :Private)
    void dav_COPY(Context *c, const QStringList &pathParts);

    C_ATTR(dav_MOVE, :Private)
    void dav_MOVE(Context *c, const QStringList &pathParts);

    C_ATTR(dav_MKCOL, :Private)
    void dav_MKCOL(Context *c, const QStringList &pathParts);

    C_ATTR(dav_PUT, :Private)
    void dav_PUT(Context *c, const QStringList &pathParts);

    C_ATTR(dav_LOCK, :Private)
    void dav_LOCK(Context *c, const QStringList &pathParts);

    C_ATTR(dav_PROPFIND, :Private)
    void dav_PROPFIND(Context *c, const QStringList &pathParts);

    C_ATTR(dav_PROPPATCH, :Private)
    void dav_PROPPATCH(Context *c, const QStringList &pathPart);

private:
    C_ATTR(Auto, :Private)
    void Auto(Context *c);

    C_ATTR(End, :Private)
    void End(Context *c) { Q_UNUSED(c); }

    void parsePropsProp(QXmlStreamReader &xml, const QString &path, QStringList &props);
    void parsePropsPropFind(QXmlStreamReader &xml, const QString &path, QStringList &props);
    bool parseProps(Context *c, const QString &path, QStringList &props);

    bool parsePropPatchValue(QXmlStreamReader &xml, const QString &path, bool set);
    bool parsePropPatchProperty(QXmlStreamReader &xml, const QString &path, bool set);
    void parsePropPatchUpdate(QXmlStreamReader &xml, const QString &path);
    bool parsePropPatch(Context *c, const QString &path);
    void profindRequest(const QFileInfo &info, QXmlStreamWriter &stream, Props prop, const QStringList &props);
    bool removeDestination(const QFileInfo &info, Response *res);

    QMimeDatabase m_db;
    QString m_baseDir;
    QHash<QString, Properties> m_pathProps;
};

#endif //WEBDAV_H

