#include "webdav.h"

#include "webdavpropertystorage.h"

#include <Cutelyst/Plugins/Authentication/authentication.h>
#include <Cutelyst/Plugins/Utils/Sql>
#include <Cutelyst/utils.h>

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

Q_LOGGING_CATEGORY(WEBDAV_PUT, "webdav.PUT")

using namespace Cutelyst;

Webdav::Webdav(QObject *parent) : Controller(parent)
{
    m_baseDir = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    if (!m_baseDir.endsWith(QLatin1Char('/'))) {
        m_baseDir.append(QLatin1Char('/'));
    }
    QDir().mkpath(m_baseDir);
    qDebug() << "BASE" << m_baseDir;

    m_propStorage = new WebdavPropertyStorage(this);
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
    qDebug() << Q_FUNC_INFO << pathParts;
    Response *res = c->response();

    const QString resource = resourcePath(c, pathParts);

    QFileInfo info(resource);
    if (info.exists()) {
        const QMimeType mime = m_db.mimeTypeForFile(resource);

        Headers &headers = res->headers();
        headers.setContentType(mime.name());
        headers.setContentDispositionAttachment(info.fileName());
        headers.setContentLength(info.size());

        const QByteArray hash = QCryptographicHash::hash(info.lastModified().toUTC().toString().toUtf8(), QCryptographicHash::Md5);
        headers.setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + QString::fromLatin1(hash.toHex()) + QLatin1Char('"'));
    } else {
        res->setStatus(Response::NotFound);
        res->setBody(QByteArrayLiteral("Content not found."));
    }
}

void Webdav::dav_GET(Context *c, const QStringList &pathParts)
{
    qDebug() << Q_FUNC_INFO << pathParts;
    Response *res = c->response();

    const QString resource = resourcePath(c, pathParts);

    auto file = new QFile(resource, c);
    if (file->open(QIODevice::ReadOnly)) {
        res->setBody(file);
        const QMimeType mime = m_db.mimeTypeForFile(resource);

        Headers &headers = res->headers();
        headers.setContentType(mime.name());
        headers.setContentDispositionAttachment(file->fileName());

        const QFileInfo info(resource);
        const QByteArray hash = QCryptographicHash::hash(info.lastModified().toUTC().toString().toUtf8(), QCryptographicHash::Md5);
        headers.setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + QString::fromLatin1(hash.toHex()) + QLatin1Char('"'));
    } else {
        QFileInfo info(resource);
        if (info.isDir()) {
            res->setStatus(Response::MethodNotAllowed);
            res->setBody(QByteArrayLiteral("This is the WebDAV interface. It can only be accessed by WebDAV clients."));
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
    qDebug() << Q_FUNC_INFO << path << resource;

    Response *res = c->response();
    QFileInfo info(resource);
    if (info.exists()) {
        if (removeDestination(info, res)) {
            res->setStatus(Response::NoContent);
            QString error;
            if (sqlFilesDelete(path, Authentication::user(c).id(), error) < 0) {
                qDebug() << "DELETE sql error" << error;
            }
        }
    } else {
        QString error;
        int ret = sqlFilesDelete(path, Authentication::user(c).id(), error);
        if (ret < 0) {
            qDebug() << "DELETE sql error" << error;
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

    qDebug() << "COPY HEADER" << req->header(QStringLiteral("DESTINATION")) << "DESTINATION PATH" << destination.path(QUrl::FullyEncoded);
    qDebug() << "COPY" << c->request()->match() << rawDestPath << destPathParts;
    qDebug() << "COPY" << path << "TO" << destPath;

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
    qDebug() << "COPY" << origInfo.absoluteFilePath() << origInfo.isDir() << origInfo.isFile();

    const QFileInfo destInfo(destResource);
    const bool overwrite = destInfo.exists();
    if (overwrite) {
        if (req->header(QStringLiteral("OVERWRITE")) == QLatin1String("F")) {
            qDebug() << "COPY: destination exists but overwrite is disallowed" << path << destPath << destination.path();
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        qWarning() << "REMOVING destination" << destInfo.absoluteFilePath();
        if (!removeDestination(destInfo, res)) {
            qWarning() << "Could NOT remove destination" << destInfo.absolutePath();
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        QString error;
        int ret = sqlFilesDelete(destPath, Authentication::user(c).id(), error);
        if (ret < 0) {
            qDebug() << "DELETE sql error" << error;
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
                qWarning() << "Failed to create SQL entry on COPY" << error;
                res->setBody(error);
                res->setStatus(Response::InternalServerError);
                QFile::remove(destInfo.absoluteFilePath());
            }
        } else {
            const QFileInfo destInfoPath(destInfo.absolutePath());
            if (!destInfoPath.exists() || !destInfoPath.isDir()) {
                qWarning() << "Destination directory does not exists or is not a directory";
                res->setStatus(Response::Conflict);
            } else {
                qWarning() << "Failed to COPY file" << path << "to" << destInfo.absoluteFilePath() << orig.errorString();
                qWarning() << "Failed list" << destination << QDir(destInfo.absolutePath()).entryList();
                res->setStatus(Response::InternalServerError);
            }
        }
    } else {
        const QString origPath = origInfo.absoluteFilePath();
        const QString destAbsPath = destInfo.absoluteFilePath();
        qDebug() << "COPY DIR" << origPath << destAbsPath;
        QDir dir;
        if (!dir.mkpath(destAbsPath)) {
            qWarning() << "Could not create destination";
            res->setStatus(Response::InternalServerError);
            return;
        }

        QString error;
        if (!sqlFilesCopy(path, destPathParts, Authentication::user(c).id(), error)) {
            qWarning() << "Failed to create SQL entry on COPY" << error;
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

    qDebug() << "MOVE" << resource << destResource;

    Response *res = c->response();

    QFileInfo destInfo(destResource);
    bool overwrite = destInfo.exists();
    if (overwrite) {
        if (req->header(QStringLiteral("OVERWRITE")) == QLatin1String("F")) {
            qDebug() << "MOVE: destination exists but overwrite is disallowed" << path << destResource;
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        if (!removeDestination(destInfo, res)) {
            qDebug() << "Destination exists and could not be removed";
            res->setStatus(Response::InternalServerError);
            return;
        }

        QString error;
        int ret = sqlFilesDelete(destPath, Authentication::user(c).id(), error);
        if (ret < 0) {
            qDebug() << "DELETE sql error" << error;
            res->setStatus(Response::InternalServerError);
            return;
        }
    }

    QFileInfo srcInfo(resource);
    qDebug() << "MOVE info" << resource << srcInfo.isFile() << srcInfo.isDir();

    if (srcInfo.isFile()) {
        QFile file(resource);
        if (file.rename(destResource)) {
            res->setStatus(overwrite ? Response::NoContent : Response::Created);
            m_propStorage->moveValues(path, destPath);
            m_propStorage->commit();
        } else {
            qDebug() << "MOVE failed" << file.errorString();
            res->setStatus(Response::InternalServerError);
            res->setBody(file.errorString());
        }

    } else if (srcInfo.isDir()) {
        QDir dir;
        if (dir.rename(resource, destResource)) {
            res->setStatus(overwrite ? Response::NoContent : Response::Created);
            m_propStorage->moveValues(path, destPath);
            m_propStorage->commit();
        } else {
            qDebug() << "MOVE dir failed";
            res->setStatus(Response::InternalServerError);
        }
    }
}

void Webdav::dav_MKCOL(Context *c, const QStringList &pathParts)
{
    qDebug() << Q_FUNC_INFO << pathParts;

    Response *res = c->response();
    if (c->request()->body()) {
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

            QString error;
            if (sqlFilesUpsert(pathParts, dirInfo, etag, Authentication::user(c).id(), error)) {
                c->response()->setStatus(Response::Created);
            } else {
                c->response()->setStatus(Response::InternalServerError);
                c->response()->setBody(error);
                dir.rmdir(resource);
                qDebug() << "MKCOL error" << error;
            }
        } else {
            qDebug() << Q_FUNC_INFO << "failed to create" << path;
            res->setStatus(Response::Conflict);

            QXmlStreamWriter stream(res);
            stream.setAutoFormatting(true);
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
    qCDebug(WEBDAV_PUT) << c->request()->uri() << c->request()->uri().toString();

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
    c->response()->setHeader(QStringLiteral("ETAG"), QLatin1Char('"') + etag + QLatin1Char('"'));

    const QFileInfo info(resource);
    QString error;
    if (sqlFilesUpsert(pathParts, info, etag, Authentication::user(c).id(), error)) {
        c->response()->setStatus(exists ? Response::OK : Response::Created);
    } else {
        c->response()->setStatus(Response::InternalServerError);
        c->response()->setBody(error);
        if (!exists) {
            file.remove();
        }
        qDebug() << "put error" << error;
    }
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT cloudlyst_put"
                               "(:path, :name, :parent_path, :mtime, :mimetype, :size, :etag, :owner_id)"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":parent_path"), pathParts.mid(0, pathParts.size() - 1).join(QLatin1Char('/')));
    query.bindValue(QStringLiteral(":name"), info.fileName());
    query.bindValue(QStringLiteral(":mtime"), info.lastModified().toUTC().toSecsSinceEpoch());
    const QMimeType mime = m_db.mimeTypeForFile(info);
    query.bindValue(QStringLiteral(":mimetype"), mime.name());
    query.bindValue(QStringLiteral(":size"), info.size());
    query.bindValue(QStringLiteral(":etag"), etag);
    query.bindValue(QStringLiteral(":owner_id"), Authentication::user(c).id());

    if (query.exec()) {
        c->response()->setStatus(exists ? Response::OK : Response::Created);
    } else {
        const QString error = query.lastError().databaseText();
        c->response()->setStatus(Response::InternalServerError);
        c->response()->setBody(error);
        if (!exists) {
            file.remove();
        }
        qDebug() << "put error" << error;
    }
}

void Webdav::dav_PROPFIND(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathFiles(pathParts);
    qDebug() << Q_FUNC_INFO << path << req->body() << req->headers();

    int depth = 0;
    const QString depthStr = req->header(QStringLiteral("DEPTH"));
    if (depthStr == QLatin1String("1")) {
        depth = 1;
    } else if (depthStr == QLatin1String("infinity")) {
        depth = -1;
    }

    qDebug() << Q_FUNC_INFO << "depth" << depth << req->header(QStringLiteral("DEPTH"));
    GetProperties props;
    if (req->body() && req->body()->size() && !parseProps(c, path, props)) {
        return;
    }

    Response *res = c->response();
    res->setStatus(Response::MultiStatus);
    res->setContentType(QStringLiteral("application/xml; charset=utf-8"));

    QXmlStreamWriter stream(res);

    const QString baseUri = QLatin1Char('/') + req->match() + QLatin1Char('/');

    const QString resource = resourcePath(c, pathParts);
    qDebug() << "***********" << resource << baseUri << path;
    if (depth != -1) {
        QSqlQuery query = CPreparedSqlQueryThreadForDB(QStringLiteral("SELECT m.name AS mimetype, f.* FROM cloudlyst.files f "
                                                                      "INNER JOIN cloudlyst.mimetypes m ON f.mimetype_id = m.id "
                                                                      "WHERE path = :path"),
                                                       QStringLiteral("cloudlyst"));
        query.bindValue(QStringLiteral(":path"), path);
        if (query.exec() && query.next()) {
            stream.setAutoFormatting(true);
            stream.writeStartDocument();
            stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
            stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

            stream.writeStartElement(QStringLiteral("d:multistatus"));

            profindRequest(query, stream, baseUri, props);

            const QString mime = query.value(QStringLiteral("mimetype")).toString();
            bool isDir = mime == QLatin1String("inode/directory");

            qDebug() << Q_FUNC_INFO << "DIR" << isDir << "DEPTH" << depth;
            qDebug() << Q_FUNC_INFO << "BASE" << req->match() << req->path();
            if (depth == 1 && isDir) {
//                const QDir dir(resource);
                qint64 parentId = query.value(QStringLiteral("id")).toLongLong();
                qDebug() << Q_FUNC_INFO << "DIR" << parentId;

                query = CPreparedSqlQueryThreadForDB(QStringLiteral("SELECT m.name AS mimetype, f.* FROM cloudlyst.files f "
                                                                    "INNER JOIN cloudlyst.mimetypes m ON f.mimetype_id = m.id "
                                                                    "WHERE parent_id = :parent_id"),
                                                     QStringLiteral("cloudlyst"));
                query.bindValue(QStringLiteral(":parent_id"), parentId);

                if (query.exec()) {
                    while (query.next()) {
                        profindRequest(query, stream, baseUri, props);
                    }
                }


//                for (const QFileInfo &info : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
//                    qDebug() << Q_FUNC_INFO << "DIR" << info.fileName();
//                    profindRequest(info, stream, baseUri, props);
//                }
            }

            stream.writeEndElement(); // multistatus

            stream.writeEndDocument();
            return;
        }

//        const QFileInfo info(resource);
//        if (info.exists()) {
//            stream.setAutoFormatting(true);
//            stream.writeStartDocument();
//            stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
//            stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

//            stream.writeStartElement(QStringLiteral("d:multistatus"));

//            profindRequest(info, stream, baseUri, props);

//            qDebug() << Q_FUNC_INFO << "DIR" << info.isDir() << "DEPTH" << depth;
//            qDebug() << Q_FUNC_INFO << "BASE" << req->match() << req->path();
//            if (depth == 1 && info.isDir()) {
//                const QDir dir(resource);
//                qDebug() << Q_FUNC_INFO << "DIR" << dir;

//                for (const QFileInfo &info : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
//                    qDebug() << Q_FUNC_INFO << "DIR" << info.fileName();
//                    profindRequest(info, stream, baseUri, props);
//                }
//            }

//            stream.writeEndElement(); // multistatus

//            stream.writeEndDocument();
//            return;
//        }
    }

    res->setStatus(Response::NotFound);

    stream.setAutoFormatting(true);
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
    qDebug() << Q_FUNC_INFO << path << req->body() << req->headers();

    if (!req->body()) {
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    int depth = 0;
    const QString depthStr = req->header(QStringLiteral("DEPTH"));
    if (depthStr == QLatin1String("1")) {
        depth = 1;
    } else if (depthStr == QLatin1String("infinity")) {
        depth = -1;
    }

    qDebug() << Q_FUNC_INFO << "depth" << depth;
    parsePropPatch(c, path);
}

void Webdav::parsePropsProp(QXmlStreamReader &xml, const QString &path, GetProperties &props)
{
    while (!xml.atEnd()) {
        auto token = xml.readNext();
        qWarning() << "PROPS token 3" <<  xml.tokenString();
        if (token == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            const QString ns = xml.namespaceUri().toString();
            qDebug() << "GET PROP" << WebdavPropertyStorage::propertyKey(name, ns);
            props.push_back({ name, ns });
            xml.skipCurrentElement();
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

void Webdav::parsePropsPropFind(QXmlStreamReader &xml, const QString &path, GetProperties &props)
{
    while (!xml.atEnd()) {
        auto token = xml.readNext();
        qWarning() << "PROPS token 2" <<  xml.tokenString();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("prop")) {
                parsePropsProp(xml, path, props);
            } else if (xml.name() == QLatin1String("allprop")) {
                qDebug() << "GET ALLPROP";
            } else if (xml.name() == QLatin1String("propname")) {
                qDebug() << "GET PROPNAME";
            }
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

bool Webdav::parseProps(Context *c, const QString &path, GetProperties &props)
{
    Response *res = c->response();

    const QByteArray data = c->request()->body()->readAll();
    qWarning() << "PROPS data" << data;
//    qDebug() << "PROPS current" << m_pathProps[path];

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        auto token = xml.readNext();
        qWarning() << "PROPS token 1" <<  xml.tokenString() << xml.name();
        if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("propfind")) {
            parsePropsPropFind(xml, path, props);
        }
    }

    if (xml.hasError()) {
        qWarning() << "PROPS parse error" << xml.errorString();

        res->setStatus(Response::BadRequest);

        QXmlStreamWriter stream(res);
        stream.setAutoFormatting(true);
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

bool Webdav::parsePropPatchValue(QXmlStreamReader &xml, const QString &path, bool set)
{
    int depth = 0;
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
        qWarning() << "PROPS token 4" << type <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            const QString name = xml.name().toString();
            if (set) {
                const QString value = xml.readElementText(QXmlStreamReader::QXmlStreamReader::SkipChildElements);
                qDebug() << "NEW PROP" << name << value << xml.tokenString();
                m_propStorage->setValue(path, WebdavPropertyStorage::propertyKey(xml.name(), xml.namespaceUri()), value);
            } else {
                qDebug() << "DELETE PROP ";
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

bool Webdav::parsePropPatchProperty(QXmlStreamReader &xml, const QString &path, bool set)
{
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
        qWarning() << "PROPS token 3" << xml.tokenType() <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("prop")) {
                qWarning() << "PROPS prop" ;
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

void Webdav::parsePropPatchUpdate(QXmlStreamReader &xml, const QString &path)
{
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();
        qWarning() << "PROPS token 2" << xml.tokenType() <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("set")) {
                qWarning() << "PROPS set" ;
                parsePropPatchProperty(xml, path, true);
            } else if (xml.name() == QLatin1String("remove")) {
                qWarning() << "PROPS remove";
                parsePropPatchProperty(xml, path, false);
            }
        } else if (type == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

bool Webdav::parsePropPatch(Context *c, const QString &path)
{
    Response *res = c->response();

    const QByteArray data = c->request()->body()->readAll();
    qWarning() << "PROP PATCH data" << data;

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        QXmlStreamReader::TokenType type = xml.readNext();

        qWarning() << "PROPS token 1" << type <<  xml.tokenString() << xml.name() << xml.text() << xml.namespaceUri();
        if (type == QXmlStreamReader::StartElement && xml.name() == QLatin1String("propertyupdate")) {
            parsePropPatchUpdate(xml, path);
            qWarning() << "parsePropPatchUpdate finished" << xml.tokenType() << xml.tokenString() << xml.atEnd() << xml.hasError() << xml.errorString();
//            QXmlStreamReader::TokenType type = xml.readNext();
//            qWarning() << "parsePropPatchUpdate finished2" << type << xml.tokenType() << xml.tokenString() << xml.atEnd() << xml.hasError() << xml.errorString();
        }
    }

    if (xml.hasError()) {
        qWarning() << "PROPS parse error" << xml.errorString();

        res->setStatus(Response::BadRequest);

        QXmlStreamWriter stream(res);
        stream.setAutoFormatting(true);
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

void Webdav::profindRequest(const QFileInfo &info, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props)
{
    const QString path = info.absoluteFilePath().mid(m_baseDir.size());
    stream.writeStartElement(QStringLiteral("d:response"));
    stream.writeTextElement(QStringLiteral("d:href"), baseUri + path);

    stream.writeStartElement(QStringLiteral("d:propstat"));

    stream.writeStartElement(QStringLiteral("d:prop"));

    GetProperties propsNotFound;

//        qDebug() << "FIND" << properties;
    for (const Property &pData : props) {
        qDebug() << "FIND data" << pData.name << pData.ns;
        bool found = false;
        if (pData.ns == QLatin1String("DAV:")) {
            if (pData.name == QLatin1String("quota-used-bytes")) {
                stream.writeEmptyElement(QStringLiteral("d:quota-used-bytes"));
                continue;
            } else if (pData.name == QLatin1String("quota-available-bytes")) {
                stream.writeEmptyElement(QStringLiteral("d:quota-available-bytes"));
                continue;
            } else if (pData.name == QLatin1String("getcontenttype")) {
                const QMimeType mime = m_db.mimeTypeForFile(info);
                stream.writeTextElement(QStringLiteral("d:getcontenttype"), mime.name());
                continue;
            } else if (pData.name == QLatin1String("getlastmodified")) {
                const QString dt = QLocale::c().toString(info.lastModified().toUTC(),
                                                         QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT"));
                stream.writeTextElement(QStringLiteral("d:getlastmodified"), dt);
                continue;
            } else if (pData.name == QLatin1String("getcontentlength")) {
                stream.writeTextElement(QStringLiteral("d:getcontentlength"), QString::number(info.size()));
                continue;
            } else if (pData.name == QLatin1String("getetag")) {
                const QByteArray hash = QCryptographicHash::hash(info.lastModified().toUTC().toString().toUtf8(), QCryptographicHash::Md5);
                stream.writeTextElement(QStringLiteral("d:getetag"), QLatin1Char('"') + QString::fromLatin1(hash.toHex()) + QLatin1Char('"'));
                continue;
            } else if (pData.name == QLatin1String("resourcetype")) {
                stream.writeStartElement(QStringLiteral("d:resourcetype"));
                if (info.isDir()) {
                    stream.writeEmptyElement(QStringLiteral("d:collection"));
                }
                stream.writeEndElement(); // resourcetype
                continue;
            }
        } else if (pData.ns == QLatin1String("http://owncloud.org/ns")) {
            if (pData.name == QLatin1String("id")) {
                stream.writeTextElement(pData.ns, QStringLiteral("id"), QStringLiteral("00123"));
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

        const QString value = m_propStorage->value(path, WebdavPropertyStorage::propertyKey(pData.name, pData.ns), found);
        if (found) {
            stream.writeTextElement(pData.ns, pData.name, value);
            qDebug() << "WRITE" << pData.name << pData.ns << value;
            continue;
        }

        propsNotFound.push_back(pData);
    }

    stream.writeEndElement(); // prop

    stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 200 OK"));

    stream.writeEndElement(); // propstat

    if (!propsNotFound.empty()) {
        stream.writeStartElement(QStringLiteral("d:propstat"));
        stream.writeStartElement(QStringLiteral("d:prop"));
        for (const Property &prop : propsNotFound) {

            stream.writeEmptyElement(prop.ns, prop.name);
            qDebug() << "WRITE 404" << prop.name << prop.ns;

        }
        stream.writeEndElement(); // prop
        stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 404 Not Found"));
        stream.writeEndElement(); // propstat
    }

    stream.writeEndElement(); // response

    m_propStorage->commit();
}

void Webdav::profindRequest(const QSqlQuery &query, QXmlStreamWriter &stream, const QString &baseUri, const GetProperties &props)
{
    const QString path = query.value(QStringLiteral("path")).toString();

    stream.writeStartElement(QStringLiteral("d:response"));
    stream.writeTextElement(QStringLiteral("d:href"), baseUri + path.midRef(6));

    stream.writeStartElement(QStringLiteral("d:propstat"));

    stream.writeStartElement(QStringLiteral("d:prop"));

    GetProperties propsNotFound;

    const QString mime = query.value(QStringLiteral("mimetype")).toString();

//        qDebug() << "FIND" << properties;
    for (const Property &pData : props) {
        qDebug() << "FIND data" << pData.name << pData.ns;
        bool found = false;
        if (pData.ns == QLatin1String("DAV:")) {
            if (pData.name == QLatin1String("quota-used-bytes")) {
                stream.writeEmptyElement(QStringLiteral("d:quota-used-bytes"));
                continue;
            } else if (pData.name == QLatin1String("quota-available-bytes")) {
                stream.writeEmptyElement(QStringLiteral("d:quota-available-bytes"));
                continue;
            } else if (pData.name == QLatin1String("getcontenttype")) {
                stream.writeTextElement(QStringLiteral("d:getcontenttype"), mime);
                continue;
            } else if (pData.name == QLatin1String("getlastmodified")) {
                const QString dt = QLocale::c().toString(QDateTime::fromSecsSinceEpoch(query.value(QStringLiteral("mtime")).toLongLong()),
                                                         QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT"));
                stream.writeTextElement(QStringLiteral("d:getlastmodified"), dt);
                continue;
            } else if (pData.name == QLatin1String("getcontentlength")) {
                stream.writeTextElement(QStringLiteral("d:getcontentlength"), QString::number(query.value(QStringLiteral("size")).toLongLong()));
                continue;
            } else if (pData.name == QLatin1String("getetag")) {
                const QString etag = query.value(QStringLiteral("etag")).toString();
                stream.writeTextElement(QStringLiteral("d:getetag"), QLatin1Char('"') + etag + QLatin1Char('"'));
                continue;
            } else if (pData.name == QLatin1String("resourcetype")) {
                stream.writeStartElement(QStringLiteral("d:resourcetype"));
                if (mime == QLatin1String("inode/directory")) {
                    stream.writeEmptyElement(QStringLiteral("d:collection"));
                }
                stream.writeEndElement(); // resourcetype
                continue;
            }
        } else if (pData.ns == QLatin1String("http://owncloud.org/ns")) {
            if (pData.name == QLatin1String("id")) {
                stream.writeTextElement(pData.ns, QStringLiteral("id"), QStringLiteral("00123"));
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

        const QString value = m_propStorage->value(path, WebdavPropertyStorage::propertyKey(pData.name, pData.ns), found);
        if (found) {
            stream.writeTextElement(pData.ns, pData.name, value);
            qDebug() << "WRITE" << pData.name << pData.ns << value;
            continue;
        }

        propsNotFound.push_back(pData);
    }

    stream.writeEndElement(); // prop

    stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 200 OK"));

    stream.writeEndElement(); // propstat

    if (!propsNotFound.empty()) {
        stream.writeStartElement(QStringLiteral("d:propstat"));
        stream.writeStartElement(QStringLiteral("d:prop"));
        for (const Property &prop : propsNotFound) {

            stream.writeEmptyElement(prop.ns, prop.name);
            qDebug() << "WRITE 404" << prop.name << prop.ns;

        }
        stream.writeEndElement(); // prop
        stream.writeTextElement(QStringLiteral("d:status"), QStringLiteral("HTTP/1.1 404 Not Found"));
        stream.writeEndElement(); // propstat
    }

    stream.writeEndElement(); // response

    m_propStorage->commit();
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
        qDebug() << "REMOVE destination DIR" << dir;
        if (dir.removeRecursively()) {
            return true;
        } else {
            res->setBody(QStringLiteral("Could not remove directory"));
            res->setStatus(Response::InternalServerError);
        }
    }
    return false;
}

bool Webdav::sqlFilesUpsert(const QStringList &pathParts, const QFileInfo &info, const QString &etag, const QVariant &userId, QString &error)
{
    const QString path = pathFiles(pathParts);
    const QString parentPath = pathFiles(pathParts.mid(0, pathParts.size() - 1));
    qDebug() << "SQL UPSERT" << path << parentPath << etag << userId;
    QSqlQuery query = CPreparedSqlQueryThreadForDB(
                QStringLiteral("SELECT cloudlyst_put"
                               "(:path, :name, :parent_path, :mtime, :mimetype, :size, :etag, :owner_id)"),
                QStringLiteral("cloudlyst"));

    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":parent_path"), parentPath);
    query.bindValue(QStringLiteral(":name"), info.fileName());
    query.bindValue(QStringLiteral(":mtime"), info.lastModified().toUTC().toSecsSinceEpoch());
    const QMimeType mime = m_db.mimeTypeForFile(info);
    query.bindValue(QStringLiteral(":mimetype"), mime.name());
    query.bindValue(QStringLiteral(":size"), info.size());
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
    qDebug() << "SQL COPY" << path << "TO" << destParentPath << destPath << destName << userId;
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
