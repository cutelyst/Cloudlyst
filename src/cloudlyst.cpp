#include "cloudlyst.h"

#include <QCoreApplication>

#include "root.h"
#include "webdav.h"

using namespace Cutelyst;

Cloudlyst::Cloudlyst(QObject *parent) : Application(parent)
{
    QCoreApplication::setApplicationName(QStringLiteral("Cloudlyst"));
}

Cloudlyst::~Cloudlyst()
{
}

bool Cloudlyst::init()
{
    new Root(this);
    new Webdav(this);

    return true;
}

