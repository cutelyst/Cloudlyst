#ifndef WEBDAV_H
#define WEBDAV_H

#include <Cutelyst/Controller>

#include <QMimeDatabase>
#include <QStorageInfo>

using namespace Cutelyst;

typedef QHash<QString, std::pair<QString, QString> > Properties;

struct FileItem
{
    QString path;
    QString name;
    QString etag;
    QString mimetype;
    qint64 mtime = -1;
    qint64 id = 0;
    qint64 size = -1;
};

struct Property
{
    QString name;
    QString ns;
};
typedef QVector<Property> GetProperties;

class QDir;
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

    virtual bool preFork(Application *app) override final;

private:
//    C_ATTR(End, :Private)
//    void End(Context *c) { Q_UNUSED(c); }

    void parsePropFindPropElement(QXmlStreamReader &xml, GetProperties &props);
    void parsePropFindElement(QXmlStreamReader &xml, GetProperties &props);
    bool parsePropFindRequest(Context *c, GetProperties &props);

    bool parsePropPatchValue(QXmlStreamReader &xml, qint64 path, bool set);
    bool parsePropPatchProperty(QXmlStreamReader &xml, qint64 path, bool set);
    void parsePropPatchUpdate(QXmlStreamReader &xml, qint64 path);
    bool parsePropPatch(Context *c, qint64 path);
    void writePropFindResponseItem(const FileItem &file, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props);
    bool removeDestination(const QFileInfo &info, Response *res);

    bool sqlFilesUpsert(const QStringList &pathParts, const QFileInfo &info, qint64 mTime, const QString &etag, const QVariant &userId, QString &error);
    bool sqlFilesCopy(const QString &path, const QStringList &destPathParts, const QVariant &userId, QString &error);
    bool sqlFilesMove(const QString &path, const QString &destPath, const QString &destName, const QVariant &userId, QString &error);
    int sqlFilesDelete(const QString &path, const QVariant &userId, QString &error);
    FileItem sqlFilesItem(const QString &path, const QVariant &userId, QString &error);
    std::vector<FileItem> sqlFilesItems(qint64 parentId, QString &error);

    inline QString pathFiles(const QStringList &pathParts) const;
    inline QString basePath(Context *c) const;
    inline QDir baseDir(Context *c) const;
    inline QString resourcePath(Context *c, const QStringList &pathParts) const;
    inline QStringList uriPathParts(const QString &path);

    QMimeDatabase m_db;
    QString m_baseDir;
    bool m_autoFormatting = true;
    QStorageInfo m_storageInfo;
    WebdavPropertyStorage *m_propStorage;
};

#endif //WEBDAV_H

