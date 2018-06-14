#include "webdav.h"

#include "webdavpropertystorage.h"

#include <QFileInfo>
#include <QDir>
#include <QDirIterator>

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <QMimeDatabase>
#include <QStandardPaths>

#include <QLoggingCategory>

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

void Webdav::dav(Context *c, const QStringList &pathParts)
{
    c->response()->setHeader(QStringLiteral("DAV"), QStringLiteral("1, 2"));
    qDebug() << "=====================" << c->req()->header("x-litmus");
}

void Webdav::dav_HEAD(Context *c, const QStringList &pathParts)
{
    qDebug() << Q_FUNC_INFO << pathParts;
    Response *res = c->response();

    const QString resource = m_baseDir + pathParts.join(QLatin1Char('/'));

    QFileInfo info(resource);
    if (info.exists()) {
        const QMimeType mime = m_db.mimeTypeForFile(resource);
        res->setContentType(mime.name());
        res->headers().setContentDispositionAttachment(info.fileName());
        res->setContentLength(info.size());
    } else {
        res->setStatus(Response::NotFound);
        res->setBody(QByteArrayLiteral("Content not found."));
    }
}

void Webdav::dav_GET(Context *c, const QStringList &pathParts)
{
    qDebug() << Q_FUNC_INFO << pathParts;
    Response *res = c->response();

    const QString resource = m_baseDir + pathParts.join(QLatin1Char('/'));

    auto file = new QFile(resource, c);
    if (file->open(QIODevice::ReadOnly)) {
        res->setBody(file);
        const QMimeType mime = m_db.mimeTypeForFile(file->fileName());
        res->setContentType(mime.name());
        res->headers().setContentDispositionAttachment(file->fileName());
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
    const QString path = pathParts.join(QLatin1Char('/'));
    const QString resource = m_baseDir + path;
    qDebug() << Q_FUNC_INFO << path << resource;

    Response *res = c->response();
    QFileInfo info(resource);
    if (info.exists()) {
        if (removeDestination(info, res)) {
            res->setStatus(Response::NoContent);
        }
    } else {
        res->setStatus(Response::NotFound);
    }
}

void Webdav::dav_COPY(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QUrl destination(req->header(QStringLiteral("DESTINATION")));
    const QString path = pathParts.join(QLatin1Char('/'));
    QString destPath = destination.path().mid(8);
    while (destPath.endsWith(QLatin1Char('/'))) {
        destPath.chop(1);
    }
    qDebug() << "COPY" << path << destPath << destination.path();

    Response *res = c->response();
    if (path == destPath) {
        res->setStatus(Response::Forbidden);
        return;
    }

    QFileInfo origInfo(m_baseDir + path);
    if (!origInfo.exists()) {
        res->setStatus(Response::NotFound);
        return;
    }
    qDebug() << "COPY" << origInfo.absoluteFilePath() << origInfo.isDir() << origInfo.isFile();

    QFileInfo destInfo(m_baseDir + destPath);
    bool overwrite = destInfo.exists();
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
    }

    if (origInfo.isFile()) {
        QFile orig(origInfo.absoluteFilePath());
        if (!orig.open(QIODevice::ReadOnly)) {
            res->setStatus(Response::NotFound);
            return;
        }

        if (orig.copy(destInfo.absoluteFilePath())) {
            res->setStatus(overwrite ? Response::NoContent : Response::Created);
        } else {
            QFileInfo destInfoPath(destInfo.absolutePath());
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
    const QString path = pathParts.join(QLatin1Char('/'));
    const QString resource = m_baseDir + path;
    const QUrl destination(c->request()->header(QStringLiteral("DESTINATION")));
    const QString destPath = destination.path().mid(8);
    QString destResource = m_baseDir + destPath;
    while (destResource.endsWith(QLatin1Char('/'))) {
        destResource.chop(1);
    }
    qDebug() << "MOVE" << resource << destResource;

    Response *res = c->response();

    QFileInfo destInfo(destResource);
    bool overwrite = destInfo.exists();
    if (overwrite) {
        if (c->req()->header(QStringLiteral("OVERWRITE")) == QLatin1String("F")) {
            qDebug() << "MOVE: destination exists but overwrite is disallowed" << path << destResource;
            res->setStatus(Response::PreconditionFailed);
            return;
        }

        if (!removeDestination(destInfo, res)) {
            qDebug() << "Destination exists and could not be removed";
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

    const QString path = pathParts.join(QLatin1Char('/'));
    const QString resource = m_baseDir + path;
    QDir dir(resource);
    if (dir.exists()) {
        res->setStatus(Response::MethodNotAllowed);
    } else if (dir.mkdir(resource)) {
        res->setStatus(Response::Created);
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

void Webdav::dav_PUT(Context *c, const QStringList &pathParts)
{
    const QString path = pathParts.join(QLatin1Char('/'));
    const QString resource = m_baseDir + path;
    qDebug() << "PUT" << path << resource;

    Request *req = c->request();
    if (!req->body()) {
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    QFile file(resource);
    if (!file.open(QFile::WriteOnly)) {
        c->response()->setStatus(Response::BadRequest);
        return;
    }

    file.write(req->body()->readAll());
    c->response()->setStatus(Response::Created);
}

void Webdav::dav_LOCK(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathParts.join(QLatin1Char('/'));
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

    const QByteArray data = req->body()->readAll();

    qDebug() << Q_FUNC_INFO << "depth" << depth << data;

    Response *res = c->response();
    res->setStatus(Response::NotImplemented);
    res->setContentType(QStringLiteral("application/xml; charset=utf-8"));

    QXmlStreamWriter stream(res);
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

void Webdav::dav_PROPFIND(Context *c, const QStringList &pathParts)
{
    Request *req = c->request();
    const QString path = pathParts.join(QLatin1Char('/'));
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
    GetProperties props;
    if (!parseProps(c, path, props)) {
        return;
    }
//    const QByteArray data = req->body()->readAll();

    Response *res = c->response();
    res->setStatus(207);
    res->setContentType(QStringLiteral("application/xml; charset=utf-8"));

    QXmlStreamWriter stream(res);

    const QString resource = m_baseDir + path;
    if (depth != -1) {
        const QFileInfo info(resource);
        if (info.exists()) {
            stream.setAutoFormatting(true);
            stream.writeStartDocument();
            stream.writeNamespace(QStringLiteral("DAV:"), QStringLiteral("d"));
            stream.writeNamespace(QStringLiteral("http://sabredav.org/ns"), QStringLiteral("s"));

            stream.writeStartElement(QStringLiteral("d:multistatus"));

            profindRequest(info, stream, Props(Resourcetype | Getcontenttype), props);

            qDebug() << Q_FUNC_INFO << "DIR" << info.isDir();
            if (depth == 1 && info.isDir()) {
                const QDir dir(resource);
                qDebug() << Q_FUNC_INFO << "DIR" << dir;

                for (const QFileInfo &info : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
                    qDebug() << Q_FUNC_INFO << "DIR" << info.fileName();
                    profindRequest(info, stream, Props(Resourcetype | Getcontenttype), props);
                }
            }

            stream.writeEndElement(); // multistatus

            stream.writeEndDocument();
            return;
        }
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

void Webdav::dav_PROPPATCH(Context *c, const QStringList &pathPart)
{
    Request *req = c->request();
    const QString path = pathPart.join(QLatin1Char('/'));
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

void Webdav::Auto(Context *c)
{

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

void Webdav::profindRequest(const QFileInfo &info, QXmlStreamWriter &stream, Props prop, const GetProperties &props)
{
    const QString path = info.absoluteFilePath().mid(m_baseDir.size());
    stream.writeStartElement(QStringLiteral("d:response"));
    stream.writeTextElement(QStringLiteral("d:href"), QLatin1String("/webdav/") + info.fileName());

    stream.writeStartElement(QStringLiteral("d:propstat"));

    stream.writeStartElement(QStringLiteral("d:prop"));

    stream.writeTextElement(QStringLiteral("d:getcontentlength"), QString::number(info.size()));

    if ((prop & Resourcetype) && info.isDir()) {
        stream.writeStartElement(QStringLiteral("d:resourcetype"));
        stream.writeEmptyElement(QStringLiteral("d:collection"));
        stream.writeEndElement(); // resourcetype
    }

    const QMimeType mime = m_db.mimeTypeForFile(info);
    stream.writeTextElement(QStringLiteral("d:getcontenttype"), mime.name());

    const QString dt = QLocale::c().toString(info.lastModified().toUTC(),
                                             QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT"));
    stream.writeTextElement(QStringLiteral("d:getlastmodified"), dt);

    GetProperties propsNotFound;
    // custom props
//    auto it = m_pathProps.constFind(path);
//    if (it != m_pathProps.constEnd()) {
//        const Properties &properties = it.value();

//        qDebug() << "FIND" << properties;
        for (const Property &pData : props) {
            qDebug() << "FIND data" << pData.name << pData.ns;
            bool found;
            const QString value = m_propStorage->value(path, WebdavPropertyStorage::propertyKey(pData.name, pData.ns), found);
            if (found) {
                stream.writeTextElement(pData.ns, pData.name, value);
                qDebug() << "WRITE" << pData.name << pData.ns << value;
                continue;
            }

            propsNotFound.push_back(pData);
        }
//    } else {
//        qDebug() << "PROPS NOT FOUND WRITE" << path << m_pathProps.keys();
//        propsNotFound = props;
//    }

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
