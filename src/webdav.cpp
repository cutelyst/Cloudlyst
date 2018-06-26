#include "webdav.h"

#include "webdavpgsqlpropertystorage.h"

#include <Cutelyst/Plugins/Authentication/authentication.h>
#include <Cutelyst/Plugins/Utils/Sql>
#include <Cutelyst/utils.h>
#include <Cutelyst/Application>

#include <QSqlQuery>
#include <QSqlError>

#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <QMimeDatabase>
#include <QCryptographicHash>
#include <QStandardPaths>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(WEBDAV_BASE, "webdav.BASE")
Q_LOGGING_CATEGORY(WEBDAV_PUT, "webdav.PUT")
Q_LOGGING_CATEGORY(WEBDAV_HEAD, "webdav.HEAD")
Q_LOGGING_CATEGORY(WEBDAV_GET, "webdav.GET")
Q_LOGGING_CATEGORY(WEBDAV_MKCOL, "webdav.MKCOL")
Q_LOGGING_CATEGORY(WEBDAV_COPY, "webdav.COPY")
Q_LOGGING_CATEGORY(WEBDAV_MOVE, "webdav.MOVE")
Q_LOGGING_CATEGORY(WEBDAV_DELETE, "webdav.DELETE")
Q_LOGGING_CATEGORY(WEBDAV_PROPFIND, "webdav.PROPFIND")
Q_LOGGING_CATEGORY(WEBDAV_PROPPATCH, "webdav.PROPPATCH")
Q_LOGGING_CATEGORY(WEBDAV_SQL, "webdav.SQL")

using namespace Cutelyst;

Webdav::Webdav(QObject *parent) : Controller(parent)
{
    m_propStorage = new WebdavPgSqlPropertyStorage(this);
}

Webdav::~Webdav()
{
}

bool Webdav::dav(Context *c, const QStringList &pathParts)
{
    Q_UNUSED(pathParts)

    if (Authentication::userExists(c) || Authentication::authenticate(c, QStringLiteral("Cloudlyst"))) {
        c->response()->setHeader(QStringLiteral("DAV"), QStringLiteral("1"));

        return true;
    }

    return false;
}

void Webdav::dav_HEAD(Context *c, const QStringList &pathParts)
{
    qCDebug(WEBDAV_HEAD) << pathParts;
    Response *res = c->response();

    const QString path = pathFiles(pathParts);

    QString error;
    FileItem fileItem = sqlFilesItem(path, Authentication::user(c).id(), error);
    if (fileItem.id) {
        Headers &headers = res->headers();
        headers.setContentType(fileItem.mimetype);
        headers.setContentDispositionAttachment(fileItem.name);
        headers.setContentLength(fileItem.size);
        headers.setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + fileItem.etag + QLatin1Char('"'));
    } else {
        qCWarning(WEBDAV_HEAD) << "error" << error;
        res->setStatus(Response::NotFound);
        res->setBody(QByteArrayLiteral("Content not found."));
    }
}

void Webdav::dav_GET(Context *c, const QStringList &pathParts)
{
    qCDebug(WEBDAV_GET) << pathParts;
    Response *res = c->response();

    const QString resource = resourcePath(c, pathParts);
    const QString path = pathFiles(pathParts);
    const QVariant userId = Authentication::user(c).id();

    QString error;
    FileItem fileItem = sqlFilesItem(path, userId, error);

    auto file = new QFile(resource, c);
    if (fileItem.id && file->open(QIODevice::ReadOnly)) {
        Headers &headers = res->headers();
        headers.setContentType(fileItem.mimetype);
        headers.setContentDispositionAttachment(fileItem.name);
        headers.setContentLength(fileItem.size);
        headers.setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + fileItem.etag + QLatin1Char('"'));

        // TODO also use X-SENDFILE
        res->setBody(file);
    } else {
        const QFileInfo info(resource);
        if (fileItem.id && info.isDir()) {
            res->setStatus(Response::MethodNotAllowed);
            res->setBody(QByteArrayLiteral("This is the WebDAV interface. It can only be accessed by WebDAV clients."));
        } else if (fileItem.id) {
            QString error;
            int ret = sqlFilesDelete(path, userId, error);
            if (ret < 0) {
                qCDebug(WEBDAV_GET) << "GET -> delete sql error" << error;
                res->setStatus(Response::InternalServerError);
                return;
            } else {
                res->setStatus(Response::Gone);
            }
        } else {
            res->setStatus(Response::NotFound);
            res->setBody(QByteArrayLiteral("Content not found."));
        }
    }
}

void Webdav::dav_DELETE(Context *c, const QStringList &pathParts)
{
    const QString path = pathFiles(pathParts);
    const QString resource = resourcePath(c, pathParts);
    qCDebug(WEBDAV_DELETE) << path << resource;

    Response *res = c->response();
    QFileInfo info(resource);
    if (info.exists()) {
        if (removeDestination(info, res)) {
            res->setStatus(Response::NoContent);
            QString error;
            if (sqlFilesDelete(path, Authentication::user(c).id(), error) < 0) {
                qCWarning(WEBDAV_DELETE) << "sql error" << error;
            }
        }
    } else {
        QString error;
        int ret = sqlFilesDelete(path, Authentication::user(c).id(), error);
        if (ret < 0) {
            qCWarning(WEBDAV_DELETE) << "sql error" << error;
        } else if (ret == 0) {
            res->setStatus(Response::NotFound);
        } else {
            res->setStatus(Response::NoContent);
        }
    }
}

void Webdav::dav_COPY(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathFiles(pathParts);
    const QString resource = resourcePath(c, pathParts);

    const QUrl destination(req->header(QStringLiteral("DESTINATION")));
    // match will usually be "webdav
    // 2 = slashes in /webdav/
    QString rawDestPath = destination.path(QUrl::FullyEncoded).mid(c->request()->match().size() + 2);
    while (rawDestPath.endsWith(QLatin1Char('/'))) {
        rawDestPath.chop(1);
    }
    const QStringList destPathParts = uriPathParts(rawDestPath);
    const QString destPath = pathFiles(destPathParts);
    const QString destResource = resourcePath(c, destPathParts);

    qCDebug(WEBDAV_COPY) << "COPY HEADER" << req->header(QStringLiteral("DESTINATION")) << "DESTINATION PATH" << destination.path(QUrl::FullyEncoded);
    qCDebug(WEBDAV_COPY) << "COPY" << c->request()->match() << rawDestPath << destPathParts;
    qCDebug(WEBDAV_COPY) << "COPY" << path << "TO" << destPath;

    Response *res = c->response();
    if (path == destPath) {
        res->setStatus(Response::Forbidden);
        return;
    }

    const QFileInfo origInfo(resource);
    if (!origInfo.exists()) {
        res->setStatus(Response::NotFound);
        return;
    }
    qCDebug(WEBDAV_COPY) << "COPY" << origInfo.absoluteFilePath() << origInfo.isDir() << origInfo.isFile();

    const QFileInfo destInfo(destResource);
    const bool overwrite = destInfo.exists();
    if (overwrite) {
        if (req->header(QStringLiteral("OVERWRITE")) == QLatin1String("F")) {
            qCDebug(WEBDAV_COPY) << "COPY: destination exists but overwrite is disallowed" << path << destPath << destination.path();
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        qCDebug(WEBDAV_COPY) << "REMOVING destination" << destInfo.absoluteFilePath();
        if (!removeDestination(destInfo, res)) {
            qCWarning(WEBDAV_COPY) << "Could NOT remove destination" << destInfo.absolutePath();
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        QString error;
        int ret = sqlFilesDelete(destPath, Authentication::user(c).id(), error);
        if (ret < 0) {
            qCDebug(WEBDAV_COPY) << "DELETE sql error" << error;
            res->setStatus(Response::InternalServerError);
            return;
        }
    }

    if (origInfo.isFile()) {
        QFile orig(origInfo.absoluteFilePath());
        if (!orig.open(QIODevice::ReadOnly)) {
            res->setStatus(Response::NotFound);
            return;
        }

        if (orig.copy(destInfo.absoluteFilePath())) {
            QString error;
            if (sqlFilesCopy(path, destPathParts, Authentication::user(c).id(), error)) {
                res->setStatus(overwrite ? Response::NoContent : Response::Created);
            } else {
                qCWarning(WEBDAV_COPY) << "Failed to create SQL entry on COPY" << error;
                res->setBody(error);
                res->setStatus(Response::InternalServerError);
                QFile::remove(destInfo.absoluteFilePath());
            }
        } else {
            const QFileInfo destInfoPath(destInfo.absolutePath());
            if (!destInfoPath.exists() || !destInfoPath.isDir()) {
                qCWarning(WEBDAV_COPY) << "Destination directory does not exists or is not a directory";
                res->setStatus(Response::Conflict);
            } else {
                qCWarning(WEBDAV_COPY) << "Failed to COPY file" << path << "to" << destInfo.absoluteFilePath() << orig.errorString();
                qCWarning(WEBDAV_COPY) << "Failed list" << destination << QDir(destInfo.absolutePath()).entryList();
                res->setStatus(Response::InternalServerError);
            }
        }
    } else {
        const QString origPath = origInfo.absoluteFilePath();
        const QString destAbsPath = destInfo.absoluteFilePath();
        qCDebug(WEBDAV_COPY) << "COPY DIR" << origPath << destAbsPath;
        QDir dir;
        if (!dir.mkpath(destAbsPath)) {
            qCWarning(WEBDAV_COPY) << "Could not create destination";
            res->setStatus(Response::InternalServerError);
            return;
        }

        QString error;
        if (!sqlFilesCopy(path, destPathParts, Authentication::user(c).id(), error)) {
            qCWarning(WEBDAV_COPY) << "Failed to create SQL entry on COPY" << error;
            res->setBody(error);
            res->setStatus(Response::InternalServerError);
            dir.rmdir(destAbsPath);
        }

        QDirIterator it(origPath, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString next = it.next();
            const QFileInfo itemInfo = it.fileInfo();
//            qDebug() << next << itemInfo.isDir() << itemInfo.isFile();
            if (itemInfo.isHidden()) {
                continue;
            }

            next.remove(0, origPath.size());
            next.prepend(destAbsPath);
            if (itemInfo.isDir()) {
                bool ret = dir.mkpath(next);
//                qDebug() << "DIR sub dir copy" << itemInfo.absoluteFilePath() << next << ret;
            } else if (itemInfo.isFile()) {
                QFile file(itemInfo.absoluteFilePath());
                bool ret = file.copy(next);
//                qDebug() << "DIR sub file copy" << itemInfo.absoluteFilePath() << next << ret << file.errorString();
            }
        }
    }
}

void Webdav::dav_MOVE(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathFiles(pathParts);
    const QString resource = resourcePath(c, pathParts);
    const QVariant userId = Authentication::user(c).id();

    const QUrl destination(req->header(QStringLiteral("DESTINATION")));
    // match will usually be "webdav
    // 2 = slashes in /webdav/
    QString rawDestPath = destination.path(QUrl::FullyEncoded).mid(c->request()->match().size() + 2);
    while (rawDestPath.endsWith(QLatin1Char('/'))) {
        rawDestPath.chop(1);
    }
    const QStringList destPathParts = uriPathParts(rawDestPath);
    const QString destPath = pathFiles(destPathParts);
    const QString destResource = resourcePath(c, destPathParts);

    qCDebug(WEBDAV_MOVE) << "MOVE" << resource << destResource;
    const QDir base = baseDir(c);
    qCDebug(WEBDAV_MOVE) << "MOVE relative" << base.relativeFilePath(resource) << base.relativeFilePath(destResource);

    Response *res = c->response();

    QFileInfo destInfo(destResource);
    bool overwrite = destInfo.exists();
    if (overwrite) {
        if (req->header(QStringLiteral("OVERWRITE")) == QLatin1String("F")) {
            qCDebug(WEBDAV_MOVE) << "MOVE: destination exists but overwrite is disallowed" << path << destResource;
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        if (!removeDestination(destInfo, res)) {
            qCWarning(WEBDAV_MOVE) << "Destination exists and could not be removed";
            res->setStatus(Response::InternalServerError);
            return;
        }

        QString error;
        int ret = sqlFilesDelete(destPath, userId, error);
        if (ret < 0) {
            qCWarning(WEBDAV_MOVE) << "DELETE sql error" << error;
            res->setStatus(Response::InternalServerError);
            return;
        }
    }

    QFileInfo srcInfo(resource);
    qCDebug(WEBDAV_MOVE) << "MOVE info" << resource << srcInfo.isFile() << srcInfo.isDir();

    if (srcInfo.isFile()) {
        QFile file(resource);
        if (file.rename(destResource)) {
            QString error;
            int ret = sqlFilesMove(base.relativeFilePath(resource), base.relativeFilePath(destResource), destination.fileName(QUrl::FullyDecoded), userId, error);
            if (ret < 0) {
                qCWarning(WEBDAV_MOVE) << "MOVE sql error" << error;
                res->setStatus(Response::InternalServerError);
                QFile::rename(destResource, resource);
                return;
            }
            res->setStatus(overwrite ? Response::NoContent : Response::Created);
        } else {
            qCWarning(WEBDAV_MOVE) << "MOVE failed" << file.errorString();
            res->setStatus(Response::InternalServerError);
            res->setBody(file.errorString());
        }

    } else if (srcInfo.isDir()) {
        QDir dir;
        if (dir.rename(resource, destResource)) {
            QString error;
            int ret = sqlFilesMove(base.relativeFilePath(resource), base.relativeFilePath(destResource), destination.fileName(QUrl::FullyDecoded), userId, error);
            if (ret < 0) {
                qCWarning(WEBDAV_MOVE) << "MOVE sql error" << error;
                res->setStatus(Response::InternalServerError);
                QFile::rename(destResource, resource);
                return;
            }

            res->setStatus(overwrite ? Response::NoContent : Response::Created);
        } else {
            qCWarning(WEBDAV_MOVE) << "MOVE dir failed";
            res->setStatus(Response::InternalServerError);
        }
    } else if (!srcInfo.exists()) {
        QString error;
        int ret = sqlFilesDelete(path, userId, error);
        if (ret < 0) {
            qCWarning(WEBDAV_MOVE) << "MOVE sql error" << error;
            res->setStatus(Response::InternalServerError);
            return;
        } else {
            res->setStatus(Response::Gone);
        }
    }
}

void Webdav::dav_MKCOL(Context *c, const QStringList &pathParts)
{
    qCDebug(WEBDAV_MKCOL) << pathParts;

    Response *res = c->response();
    if (c->request()->body()) {
        qCDebug(WEBDAV_MKCOL) << "MKCOL must not come with a body";
        res->setStatus(Response::UnsupportedMediaType);
        return;
    }

    const QString path = pathFiles(pathParts);
    const QString resource = resourcePath(c, pathParts);
    QDir dir(resource);
    if (dir.exists()) {
        res->setStatus(Response::MethodNotAllowed);
    } else {
        if (dir.mkdir(resource)) {
            QFileInfo dirInfo(resource);
            const QByteArray hash = QCryptographicHash::hash(dirInfo.lastModified().toUTC().toString(Qt::ISODate).toLatin1(), QCryptographicHash::Md5);
            const QString etag = QString::fromLatin1(hash.toHex());
            c->response()->setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + etag + QLatin1Char('"'));

            const qint64 ocMTime = c->request()->header(QStringLiteral("X_OC_MTIME")).toLongLong();

            QString error;
            if (sqlFilesUpsert(pathParts, dirInfo, ocMTime, etag, Authentication::user(c).id(), error)) {
                if (ocMTime) {
                    c->response()->setHeader(QStringLiteral("X_OC_MTIME"), QStringLiteral("accepted"));
                }
                c->response()->setStatus(Response::Created);
            } else {
                c->response()->setStatus(Response::InternalServerError);
                c->response()->setBody(error);
                dir.rmdir(resource);
                qCWarning(WEBDAV_MKCOL) << "error" << error;
            }
        } else {
            qCWarning(WEBDAV_MKCOL) << "failed to create" << path;
            res->setStatus(Response::Conflict);

            QXmlStreamWriter stream(res);
            stream.setAutoFormatting(m_autoFormatting);
            stream.writeStartDocument();
            stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
            stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

            stream.writeStartElement(QStringLiteral("d:error"));
            stream.writeTextElement(QStringLiteral("s:exception"), QStringLiteral("Sabre\\DAV\\Exception\\NotFound"));
            stream.writeTextElement(QStringLiteral("s:message"), QLatin1String("File with name /") + path + QLatin1String(" could not be located"));
            stream.writeEndElement(); // error

            stream.writeEndDocument();
        }
    }
}

void Webdav::dav_PUT(Context *c, const QStringList &pathParts)
{
    const QString path = pathFiles(pathParts);
    const QString resource = resourcePath(c, pathParts);
    qCDebug(WEBDAV_PUT) << path << resource;
    qCDebug(WEBDAV_PUT) << c->request()->uri() << c->request()->uri().toString() << c->request()->headers();

    Request *req = c->request();
    if (!req->body()) {
        qCWarning(WEBDAV_PUT) << "Missing body";
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    QFile file(resource);
    bool exists = file.exists();
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        qCWarning(WEBDAV_PUT) << "Could not open file for writting" << file.errorString();
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    QIODevice *uploadIO = req->body();
    QCryptographicHash hash(QCryptographicHash::Md5);
    char block[64 * 1024];
    while (!uploadIO->atEnd()) {
        qint64 in = uploadIO->read(block, sizeof(block));
        if (in <= 0) {
            break;
        }

        if (file.write(block, in) != in) {
            qCWarning(WEBDAV_PUT) << "Failed to write body";
            break;
        }
        hash.addData(block, in);
    }
    file.close();

    const QString etag = QString::fromLatin1(hash.result().toHex());

    const QFileInfo info(resource);

    const qint64 ocMTime = c->request()->header(QStringLiteral("X_OC_MTIME")).toLongLong();

    QString error;
    if (sqlFilesUpsert(pathParts, info, ocMTime, etag, Authentication::user(c).id(), error)) {
        if (ocMTime) {
            c->response()->setHeader(QStringLiteral("X_OC_MTIME"), QStringLiteral("accepted"));
        }
        c->response()->setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + etag + QLatin1Char('"'));
        c->response()->setStatus(exists ? Response::OK : Response::Created);
    } else {
        qCWarning(WEBDAV_PUT) << "put error" << error;
        c->response()->setStatus(Response::InternalServerError);
        c->response()->setBody(error);
        if (!exists) {
            file.remove();
        }
    }
}

void Webdav::dav_PROPFIND(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathFiles(pathParts);
    qCDebug(WEBDAV_PROPFIND) << path << req->body() << req->headers();

    int depth = 0;
    const QString depthStr = req->header(QStringLiteral("DEPTH"));
    if (depthStr == QLatin1String("1")) {
        depth = 1;
    } else if (depthStr == QLatin1String("infinity")) {
        depth = -1;
    }

    qCDebug(WEBDAV_PROPFIND) << "depth" << depth << req->header(QStringLiteral("DEPTH"));
    GetProperties props;
    if (req->body() && req->body()->size() && !parsePropFindRequest(c, props)) {
        return;
    }

    Response *res = c->response();
    res->setStatus(Response::MultiStatus);
    res->setContentType(QStringLiteral("application/xml; charset=utf-8"));

    QXmlStreamWriter stream(res);

    const QString baseUri = QLatin1Char('/') + req->match() + QLatin1Char('/');

    const QString resource = resourcePath(c, pathParts);
    qCDebug(WEBDAV_PROPFIND) << "***********" << resource << baseUri << path;
    if (depth != -1) {
        const QVariant userId = Authentication::user(c).id();
        QString error;
        FileItem file = sqlFilesItem(path, userId, error);

        if (file.id) {

            stream.setAutoFormatting(m_autoFormatting);
            stream.writeStartDocument();
            stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
            stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

            stream.writeStartElement(QStringLiteral("d:multistatus"));

            writePropFindResponseItem(file, stream, baseUri, props);

            const QString mime = file.mimetype;
            bool isDir = mime == QLatin1String("httpd/unix-directory");

            qCDebug(WEBDAV_PROPFIND) << "DIR" << isDir << "DEPTH" << depth;
            qCDebug(WEBDAV_PROPFIND) << "BASE" << req->match() << req->path();
            if (depth == 1 && isDir) {
                qint64 parentId = file.id;
                qCDebug(WEBDAV_PROPFIND) << "DIR" << parentId;

                std::vector<FileItem> files = sqlFilesItems(parentId, error);

                for (const FileItem &file : files) {
                    writePropFindResponseItem(file, stream, baseUri, props);
                }
            }

            stream.writeEndElement(); // multistatus

            stream.writeEndDocument();
            return;
        }
    }

    res->setStatus(Response::NotFound);

    stream.setAutoFormatting(m_autoFormatting);
    stream.writeStartDocument();
    stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
    stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

    stream.writeStartElement(QStringLiteral("d:error"));
    stream.writeTextElement(QStringLiteral("s:exception"), QStringLiteral("Sabre\\DAV\\Exception\\NotFound"));
    stream.writeTextElement(QStringLiteral("s:message"), QLatin1String("File with name ") + path + QLatin1String(" could not be located"));
    stream.writeEndElement(); // error

    stream.writeEndDocument();
}

void Webdav::dav_PROPPATCH(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathFiles(pathParts);
    qCDebug(WEBDAV_PROPPATCH) << path << req->body() << req->headers();

    if (!req->body()) {
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    QString error;
    FileItem item = sqlFilesItem(path, Authentication::user(c).id(), error);
    if (item.id) {
        parsePropPatch(c, item.id);
    } else {
        qCWarning(WEBDAV_PROPPATCH) << "Not found" << path << error;
    }
}

bool Webdav::preFork(Application *app)
{
    m_baseDir = app->config(QStringLiteral("DataDir"), QStandardPaths::writableLocation(QStandardPaths::DataLocation)).toString();
    if (!m_baseDir.endsWith(QLatin1Char('/'))) {
        m_baseDir.append(QLatin1Char('/'));
    }
    QDir().mkpath(m_baseDir);
    qCDebug(WEBDAV_BASE) << "BASE" << m_baseDir;
    m_storageInfo.setPath(m_baseDir);

    m_autoFormatting = app->config(QStringLiteral("XmlAutoFormatting"), false).toBool();
}

void Webdav::parsePropFindPropElement(QXmlStreamReader &xml, GetProperties &props)
{
    while (!xml.atEnd()) {
        auto token = xml.readNext();
//        qCDebug(WEBDAV_PROPFIND) << "PROPS token 3" <<  xml.tokenString();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            const QString ns = xml.namespaceUri().toString();
            qCDebug(WEBDAV_PROPFIND) << "GET PROP" << WebdavPropertyStorage::propertyKey(name, ns);
            props.push_back({ name, ns });
            xml.skipCurrentElement();
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

void Webdav::parsePropFindElement(QXmlStreamReader &xml, GetProperties &props)
{
    while (!xml.atEnd()) {
        auto token = xml.readNext();
//        qCDebug(WEBDAV_PROPFIND) << "PROPS token 2" <<  xml.tokenString();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("prop")) {
                parsePropFindPropElement(xml, props);
            } else if (xml.name() == QLatin1String("allprop")) {
                qCDebug(WEBDAV_PROPFIND) << "GET ALLPROP";
                const QString davNS = QStringLiteral("DAV:");
                props.append({
                                 {QStringLiteral("quota-used-bytes"), davNS},
                                 {QStringLiteral("quota-available-bytes"), davNS},
                                 {QStringLiteral("getcontenttype"), davNS},
                                 {QStringLiteral("getlastmodified"), davNS},
                                 {QStringLiteral("getcontentlength"), davNS},
                                 {QStringLiteral("getetag"), davNS},
                                 {QStringLiteral("resourcetype"), davNS},
                             });
            } else if (xml.name() == QLatin1String("propname")) {
                qCDebug(WEBDAV_PROPFIND) << "GET PROPNAME";
            }
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

bool Webdav::parsePropFindRequest(Context *c, GetProperties &props)
{
    Response *res = c->response();

    const QByteArray data = c->request()->body()->readAll();
    qCDebug(WEBDAV_PROPFIND) << "PROPS data" << data;
//    qDebug() << "PROPS current" << m_pathProps[path];

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        auto token = xml.readNext();
//        qCDebug(WEBDAV_PROPFIND) << "PROPS token 1" <<  xml.tokenString() << xml.name();
        if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("propfind")) {
            parsePropFindElement(xml, props);
        }
    }

    if (xml.hasError()) {
        qCWarning(WEBDAV_PROPFIND) << "PROPS parse error" << xml.errorString();

        res->setStatus(Response::BadRequest);

        QXmlStreamWriter stream(res);
        stream.setAutoFormatting(m_autoFormatting);
        stream.writeStartDocument();
        stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
        stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

        stream.writeStartElement(QStringLiteral("d:error"));
        stream.writeTextElement(QStringLiteral("s:exception"), QStringLiteral("Sabre\\DAV\\Exception\\NotFound"));
        stream.writeTextElement(QStringLiteral("s:message"), xml.errorString());
        stream.writeEndElement(); // error

        stream.writeEndDocument();
        return false;
    }

    return true;
}

bool Webdav::parsePropPatchValue(QXmlStreamReader &xml, qint64 path, bool set)
{
    int depth = 0;
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
        qCDebug(WEBDAV_PROPPATCH) << "PROPS token 4" << type <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            if (set) {
                const QString value = xml.readElementText(QXmlStreamReader::QXmlStreamReader::SkipChildElements);
                qCDebug(WEBDAV_PROPPATCH) << "NEW PROP" << name << value << xml.tokenString();
                m_propStorage->setValue(path, WebdavPropertyStorage::propertyKey(xml.name(), xml.namespaceUri()), value);
            } else {
                qCDebug(WEBDAV_PROPPATCH) << "DELETE PROP ";
                m_propStorage->remove(path, WebdavPropertyStorage::propertyKey(xml.name(), xml.namespaceUri()));
                xml.skipCurrentElement();
            }
            ++depth;
        } else if (type == QXmlStreamReader::EndElement) {
            if (--depth == 0) {
                return true;
            }
        }
    }
    return false;
}

bool Webdav::parsePropPatchProperty(QXmlStreamReader &xml, qint64 path, bool set)
{
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
//        qCDebug(WEBDAV_PROPPATCH) << "PROPS token 3" << xml.tokenType() <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("prop")) {
//                qCDebug(WEBDAV_PROPPATCH) << "PROPS prop" ;
                if (!parsePropPatchValue(xml, path, set)) {
                    return false;
                }
                continue;
            }
        } else {
            return true;
        }
        return true;
    }
    return true;
}

void Webdav::parsePropPatchUpdate(QXmlStreamReader &xml, qint64 path)
{
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
//        qCDebug(WEBDAV_PROPPATCH) << "PROPS token 2" << xml.tokenType() <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("set")) {
                qCDebug(WEBDAV_PROPPATCH) << "PROPS set" ;
                parsePropPatchProperty(xml, path, true);
            } else if (xml.name() == QLatin1String("remove")) {
                qCDebug(WEBDAV_PROPPATCH) << "PROPS remove";
                parsePropPatchProperty(xml, path, false);
            }
        } else if (type == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

bool Webdav::parsePropPatch(Context *c, qint64 path)
{
    Response *res = c->response();

    const QByteArray data = c->request()->body()->readAll();
    qCDebug(WEBDAV_PROPPATCH) << "PROP PATCH data" << data;

    m_propStorage->begin();

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();

//        qCDebug(WEBDAV_PROPPATCH) << "PROPS token 1" << type <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement && xml.name() == QLatin1String("propertyupdate")) {
            parsePropPatchUpdate(xml, path);
            qCDebug(WEBDAV_PROPPATCH) << "parsePropPatchUpdate finished" << xml.tokenType() << xml.tokenString() << xml.atEnd() << xml.hasError() << xml.errorString();
//            QXmlStreamReader::TokenType type = xml.readNext();
//            qWarning() << "parsePropPatchUpdate finished2" << type << xml.tokenType() << xml.tokenString() << xml.atEnd() << xml.hasError() << xml.errorString();
        }
    }

    if (xml.hasError()) {
        qCWarning(WEBDAV_PROPPATCH) << "PROPS parse error" << xml.errorString();
        m_propStorage->rollback();

        res->setStatus(Response::BadRequest);

        QXmlStreamWriter stream(res);
        stream.setAutoFormatting(m_autoFormatting);
        stream.writeStartDocument();
        stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
        stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

        stream.writeStartElement(QStringLiteral("d:error"));
        stream.writeTextElement(QStringLiteral("s:exception"), QStringLiteral("Sabre\\DAV\\Exception\\NotFound"));
        stream.writeTextElement(QStringLiteral("s:message"), xml.errorString());
        stream.writeEndElement(); // error

        stream.writeEndDocument();
        return false;
    }
    m_propStorage->commit();

    return true;
}

void Webdav::writePropFindResponseItem(const FileItem &file, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props)
{
    const QString path = file.path;

    stream.writeStartElement(QStringLiteral("d:response"));
    stream.writeTextElement(QStringLiteral("d:href"), baseUri + path.midRef(6));

    stream.writeStartElement(QStringLiteral("d:propstat"));

    stream.writeStartElement(QStringLiteral("d:prop"));

    QHash<QString, Property> propsNotFound;

    const QString mime = file.mimetype;

//        qDebug() << "FIND" << properties;
    for (const Property &pData : props) {
//        qCDebug(WEBDAV_PROPFIND) << "FIND data" << pData.ns << pData.name;
        if (pData.ns == QLatin1String("DAV:")) {
            if (pData.name == QLatin1String("quota-used-bytes")) {
                if (mime == QLatin1String("httpd/unix-directory")) {
                    stream.writeTextElement(QStringLiteral("d:quota-used-bytes"), QString::number(file.size));
                } else {
                    stream.writeEmptyElement(QStringLiteral("d:quota-used-bytes"));
                }
                continue;
            } else if (pData.name == QLatin1String("quota-available-bytes")) {
                if (mime == QLatin1String("httpd/unix-directory")) {
                    m_storageInfo.refresh();
                    stream.writeTextElement(QStringLiteral("d:quota-available-bytes"), QString::number(m_storageInfo.bytesAvailable()));
                } else {
                    stream.writeEmptyElement(QStringLiteral("d:quota-available-bytes"));
                }
                continue;
            } else if (pData.name == QLatin1String("getcontenttype")) {
                stream.writeTextElement(QStringLiteral("d:getcontenttype"), mime);
                continue;
            } else if (pData.name == QLatin1String("getlastmodified")) {
                const QString dt = QLocale::c().toString(QDateTime::fromSecsSinceEpoch(file.mtime).toUTC(),
                                                         QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT"));
                stream.writeTextElement(QStringLiteral("d:getlastmodified"), dt);
                continue;
            } else if (pData.name == QLatin1String("getcontentlength")) {
                stream.writeTextElement(QStringLiteral("d:getcontentlength"), QString::number(file.size));
                continue;
            } else if (pData.name == QLatin1String("getetag")) {
                const QString etag = file.etag;
                stream.writeTextElement(QStringLiteral("d:getetag"), QLatin1Char('"') + etag + QLatin1Char('"'));
                continue;
            } else if (pData.name == QLatin1String("resourcetype")) {
                stream.writeStartElement(QStringLiteral("d:resourcetype"));
                if (mime == QLatin1String("httpd/unix-directory")) {
                    stream.writeEmptyElement(QStringLiteral("d:collection"));
                }
                stream.writeEndElement(); // resourcetype
                continue;
            }
        } else if (pData.ns == QLatin1String("http://owncloud.org/ns")) {
            if (pData.name == QLatin1String("id")) {
                stream.writeTextElement(pData.ns, QStringLiteral("id"), QString::number(file.id));
                continue;
            } else if (pData.name == QLatin1String("downloadURL")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("downloadURL"));
                continue;
            } else if (pData.name == QLatin1String("permissions")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("permissions"));
                continue;
            } else if (pData.name == QLatin1String("data-fingerprint")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("data-fingerprint"));
                continue;
            } else if (pData.name == QLatin1String("share-types")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("share-types"));
                continue;
            } else if (pData.name == QLatin1String("dDC")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("dDC"));
                continue;
            } else if (pData.name == QLatin1String("checksums")) {
                stream.writeEmptyElement(pData.ns, QStringLiteral("checksums"));
                continue;
            }
        }

        propsNotFound.insert(WebdavPropertyStorage::propertyKey(pData.name, pData.ns), pData);
    }

    // avoid doing SQL calls as much as possible
    if (!propsNotFound.empty()) {
        QSqlQuery query = CPreparedSqlQueryThreadForDB(QStringLiteral("SELECT name, value "
                                                                      "FROM cloudlyst.file_properties "
                                                                      "WHERE file_id = :file_id"),
                                                       QStringLiteral("cloudlyst"));
        query.bindValue(QStringLiteral(":file_id"), file.id);

        if (query.exec()) {
            while (query.next()) {
                const QString key = query.value(0).toString();

                auto it = propsNotFound.constFind(key);
                if (it != propsNotFound.constEnd()) {
                    const Property &pData = it.value();
                    const QString value = query.value(1).toString();
                    qWarning() << "FOUND property" << key << value;
                    stream.writeTextElement(pData.ns, pData.name, value);
                    propsNotFound.erase(it);
                }
            }
        } else {
            qCWarning(WEBDAV_PROPFIND) << "FAILED to exec" << query.lastError().databaseText();
        }
    }

    stream.writeEndElement(); // prop

    stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 200 OK"));

    stream.writeEndElement(); // propstat

    if (!propsNotFound.empty()) {
        stream.writeStartElement(QStringLiteral("d:propstat"));
        stream.writeStartElement(QStringLiteral("d:prop"));

        for (const Property &prop : propsNotFound) {
            qCDebug(WEBDAV_PROPFIND) << "WRITE 404" << prop.ns << prop.name;
            stream.writeEmptyElement(prop.ns, prop.name);
        }

        stream.writeEndElement(); // prop
        stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 404 Not Found"));
        stream.writeEndElement(); // propstat
    }

    stream.writeEndElement(); // response
}

bool Webdav::removeDestination(const QFileInfo &info, Response *res)
{
    if (info.isFile()) {
        QFile file(info.absoluteFilePath());
        if (file.remove()) {
            return true;
        } else {
            res->setBody(file.errorString());
            res->setStatus(Response::InternalServerError);
        }
    } else if (info.isDir()) {
        QDir dir(info.absoluteFilePath());
        if (dir.removeRecursively()) {
            return true;
        } else {
            res->setBody(QStringLiteral("Could not remove directory"));
            res->setStatus(Response::InternalServerError);
        }
    }
    return false;
}

bool Webdav::sqlFilesUpsert(const QStringList &pathParts, const QFileInfo &info, qint64 mTime, const QString &etag, const QVariant &userId, QString &error)
{
    const QString path = pathFiles(pathParts);
    const QString parentPath = pathFiles(pathParts.mid(0, pathParts.size() - 1));
    qCDebug(WEBDAV_SQL) << "SQL UPSERT" << path << parentPath << etag << userId;
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT cloudlyst_put"
                               "(:path, :name, :parent_path, :mtime, :storage_mtime, :mimetype, :size, :etag, :owner_id)"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":parent_path"), parentPath);
    query.bindValue(QStringLiteral(":name"), info.fileName());
    if (mTime) {
        query.bindValue(QStringLiteral(":mtime"), mTime);
    } else {
        query.bindValue(QStringLiteral(":mtime"), info.lastModified().toSecsSinceEpoch());
    }

    query.bindValue(QStringLiteral(":storage_mtime"), info.lastModified().toSecsSinceEpoch());

    if (info.isDir()) {
        query.bindValue(QStringLiteral(":mimetype"), QStringLiteral("httpd/unix-directory"));
        query.bindValue(QStringLiteral(":size"), 0);
    } else {
        const QMimeType mime = m_db.mimeTypeForFile(info);
        query.bindValue(QStringLiteral(":mimetype"), mime.name());
        query.bindValue(QStringLiteral(":size"), info.size());
    }

    query.bindValue(QStringLiteral(":etag"), etag);
    query.bindValue(QStringLiteral(":owner_id"), userId);

    if (query.exec()) {
        return true;
    } else {
        error = query.lastError().databaseText();
        return false;
    }
}

bool Webdav::sqlFilesCopy(const QString &path, const QStringList &destPathParts, const QVariant &userId, QString &error)
{
    const QString destPath = pathFiles(destPathParts);
    const QString destParentPath = pathFiles(destPathParts.mid(0, destPathParts.size() - 1));
    const QString destName = destPathParts.last();
    qCDebug(WEBDAV_SQL) << "SQL COPY" << path << "TO" << destParentPath << destPath << destName << userId;
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT cloudlyst_copy"
                               "(:path, :dest_parent_path, :dest_path, :dest_name, :owner_id)"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":dest_parent_path"), destParentPath);
    query.bindValue(QStringLiteral(":dest_path"), destPath);
    query.bindValue(QStringLiteral(":dest_name"), destName);
    query.bindValue(QStringLiteral(":owner_id"), userId);

    if (query.exec()) {
        return true;
    } else {
        error = query.lastError().databaseText();
        return false;
    }
}

bool Webdav::sqlFilesMove(const QString &path, const QString &destPath, const QString &destName, const QVariant &userId, QString &error)
{
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT cloudlyst_move(:path, :destPath, :destName, :owner_id)"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":destPath"), destPath);
    query.bindValue(QStringLiteral(":destName"), destName);
    query.bindValue(QStringLiteral(":owner_id"), userId);

    if (query.exec()) {
        return query.numRowsAffected();
    } else {
        error = query.lastError().databaseText();
        return -1;
    }
}

int Webdav::sqlFilesDelete(const QString &path, const QVariant &userId, QString &error)
{
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("DELETE FROM cloudlyst.files WHERE path = :path AND owner_id = :owner_id"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":owner_id"), userId);

    if (query.exec()) {
        return query.numRowsAffected();
    } else {
        error = query.lastError().databaseText();
        return -1;
    }
}

FileItem Webdav::sqlFilesItem(const QString &path, const QVariant &userId, QString &error)
{
    FileItem ret;

    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT f.id, f.path, f.name, f.size, m.name, f.etag, f.mtime "
                               "FROM cloudlyst.files f "
                               "INNER JOIN  cloudlyst.mimetypes m ON m.id = f.mimetype_id "
                               "WHERE f.path = :path AND f.owner_id = :owner_id"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":owner_id"), userId);

    if (query.exec() && query.next()) {
        ret.id = query.value(0).toLongLong();
        ret.path = query.value(1).toString();
        ret.name = query.value(2).toString();
        ret.size = query.value(3).toLongLong();
        ret.mimetype = query.value(4).toString();
        ret.etag = query.value(5).toString();
        ret.mtime = query.value(6).toLongLong();
    } else {
        error = query.lastError().databaseText();
        return ret;
    }
}

std::vector<FileItem> Webdav::sqlFilesItems(qint64 parentId, QString &error)
{
    std::vector<FileItem> rets;
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT f.id, f.path, f.name, f.size, m.name, f.etag, f.mtime "
                               "FROM cloudlyst.files f "
                               "INNER JOIN cloudlyst.mimetypes m ON m.id = f.mimetype_id "
                               "WHERE parent_id = :parent_id"),
                QStringLiteral("cloudlyst"));
    query.bindValue(QStringLiteral(":parent_id"), parentId);

    if (query.exec()) {
        while (query.next()) {
            FileItem ret;
            ret.id = query.value(0).toLongLong();
            ret.path = query.value(1).toString();
            ret.name = query.value(2).toString();
            ret.size = query.value(3).toLongLong();
            ret.mimetype = query.value(4).toString();
            ret.etag = query.value(5).toString();
            ret.mtime = query.value(6).toLongLong();
            rets.push_back(ret);
        }
    } else {
        error = query.lastError().databaseText();
        return rets;
    }
}

QString Webdav::pathFiles(const QStringList &pathParts) const
{
    if (pathParts.isEmpty()) {
        return QStringLiteral("files");
    }

    return QLatin1String("files/") + pathParts.join(QLatin1Char('/'));
}

QString Webdav::basePath(Context *c) const
{
    return m_baseDir + Authentication::user(c).value(QStringLiteral("username")).toString() + QLatin1Char('/');
}

QDir Webdav::baseDir(Context *c) const
{
    return QDir(basePath(c));
}

QString Webdav::resourcePath(Context *c, const QStringList &pathParts) const
{
    return basePath(c) + pathFiles(pathParts);
}

QStringList Webdav::uriPathParts(const QString &path)
{
    QStringList ret;
    QVector<QStringRef> parts = path.splitRef(QLatin1Char('/'));
    for (const QStringRef strRef : parts) {
        QByteArray ba = strRef.toLatin1();
        ret.append(Utils::decodePercentEncoding(&ba));
    }
    return ret;
}
