/*
 * This file was generated by qdbusxml2cpp version 0.7
 * Command line was: qdbusxml2cpp inteface.xml -p mdecoratordbusinterface -c MDecoratorDBusInterface -i mabstractappinterface.h
 *
 * qdbusxml2cpp is Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This is an auto-generated file.
 * Do not edit! All changes made to it will be lost.
 */

#ifndef MDECORATORDBUSINTERFACE_H_1295881708
#define MDECORATORDBUSINTERFACE_H_1295881708

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtDBus/QtDBus>
#include "mabstractappinterface.h"

/*
 * Proxy class for interface com.nokia.MDecorator
 */
class MDecoratorDBusInterface: public QDBusAbstractInterface
{
    Q_OBJECT
public:
    static inline const char *staticInterfaceName()
    { return "com.nokia.MDecorator"; }

public:
    MDecoratorDBusInterface(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent = 0);

    ~MDecoratorDBusInterface();

public Q_SLOTS: // METHODS
    inline QDBusPendingReply<> setActions(IPCActionList actions, uint window)
    {
        QList<QVariant> argumentList;
        argumentList << qVariantFromValue(actions) << qVariantFromValue(window);
        return asyncCallWithArgumentList(QLatin1String("setActions"), argumentList);
    }

Q_SIGNALS: // SIGNALS
    void toggled(const QString &uuid, bool checked);
    void triggered(const QString &uuid, bool checked);
};

namespace com {
  namespace nokia {
    typedef ::MDecoratorDBusInterface MDecorator;
  }
}
#endif