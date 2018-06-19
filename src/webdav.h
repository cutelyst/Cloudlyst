#ifndef WEBDAV_H
#define WEBDAV_H

#include <Cutelyst/Controller>

#include <QMimeDatabase>

using namespace Cutelyst;

typedef QHash<QString, std::pair<QString, QString> > Properties;

struct Property
{
    QString name;
    QString ns;
};
typedef std::vector<Property> GetProperties;

class QSqlQuery;
class QFileInfo;
class QXmlStreamReader;
class QXmlStreamWriter;
class WebdavPropertyStorage;
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
    bool dav(Context *c, const QStringList &pathParts);

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

    C_ATTR(dav_PROPFIND, :Private)
    void dav_PROPFIND(Context *c, const QStringList &pathParts);

    C_ATTR(dav_PROPPATCH, :Private)
    void dav_PROPPATCH(Context *c, const QStringList &pathParts);

private:
//    C_ATTR(End, :Private)
//    void End(Context *c) { Q_UNUSED(c); }

    void parsePropsProp(QXmlStreamReader &xml, const QString &path, GetProperties &props);
    void parsePropsPropFind(QXmlStreamReader &xml, const QString &path, GetProperties &props);
    bool parseProps(Context *c, const QString &path, GetProperties &props);

    bool parsePropPatchValue(QXmlStreamReader &xml, const QString &path, bool set);
    bool parsePropPatchProperty(QXmlStreamReader &xml, const QString &path, bool set);
    void parsePropPatchUpdate(QXmlStreamReader &xml, const QString &path);
    bool parsePropPatch(Context *c, const QString &path);
    void profindRequest(const QFileInfo &info, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props);
    void profindRequest(const QSqlQuery &query, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props);
    bool removeDestination(const QFileInfo &info, Response *res);

    bool sqlFilesUpsert(const QString &path, const QString &parentPath, const QFileInfo &info, const QString &etag, const QVariant &userId, QString &error);
    bool sqlFilesDelete(const QString &path, const QVariant &userId, QString &error);

    inline QString pathFiles(const QStringList &pathParts) const;
    inline QString basePath(Context *c) const;
    inline QString resourcePath(Context *c, const QStringList &pathParts) const;

    QMimeDatabase m_db;
    QString m_baseDir;
    WebdavPropertyStorage *m_propStorage;
};

#endif //WEBDAV_H

