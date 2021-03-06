/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2012 Jolla Ltd.
** Contact: Vesa Halttunen (vesa.halttunen@jollamobile.com)
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include <QApplication>
#include <QTranslator>
#include <QtDeclarative>
#include "mdecoratorwindow.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTranslator engineeringEnglish;
    engineeringEnglish.load("recovery", "/usr/share/translations");
    app.installTranslator(&engineeringEnglish);
    QTranslator translator;
    translator.load(QLocale(), "recovery", "-", "/usr/share/translations");
    app.installTranslator(&translator);

    qmlRegisterUncreatableType<MDecoratorWindow>("org.nemomobile.mdecorator", 0, 1, "MDecoratorWindow", "This type is initialized by main");
    MDecoratorWindow window;

    return app.exec();
}
