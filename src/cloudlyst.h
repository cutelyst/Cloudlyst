#ifndef CLOUDLYST_H
#define CLOUDLYST_H

#include <Cutelyst/Application>

using namespace Cutelyst;

class Cloudlyst : public Application
{
    Q_OBJECT
    CUTELYST_APPLICATION(IID "Cloudlyst")
public:
    Q_INVOKABLE explicit Cloudlyst(QObject *parent = 0);
    ~Cloudlyst();

    bool init();
};

#endif //CLOUDLYST_H

