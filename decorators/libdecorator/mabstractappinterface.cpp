/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include <QtDebug>

#include "mabstractappinterface.h"
#include <mrmiserver.h>
#include <mrmiclient.h>
#include <QX11Info>
#include <QRect>
#include <QRegion>
#include <QDesktopWidget>
#include <QApplication>
#include <QMenu>

QDataStream &operator<<(QDataStream &out, const IPCAction &myObj)
{
    out << myObj.m_key;
    out << myObj.m_text;
    out << myObj.m_checkable;
    out << myObj.m_checked;
    out << (int)myObj.m_type;
    out << myObj.m_icon;
    return out;
}

QDataStream &operator>>(QDataStream &in, IPCAction &myObj)
{
    int type;
    in >> myObj.m_key;
    in >> myObj.m_text;
    in >> myObj.m_checkable;
    in >> myObj.m_checked;
    in >> type;
    in >> myObj.m_icon;
    myObj.m_type = (IPCAction::ActionType)type;
    return in;
}

class MAbstractAppInterfacePrivate
{
public:

    MRmiClient* remote_app;
    MAbstractAppInterface* q_ptr;
};

MAbstractAppInterface::MAbstractAppInterface(QObject *parent)
    : QObject(parent),
      d_ptr(new MAbstractAppInterfacePrivate())
{
    Q_D(MAbstractAppInterface);

    qRegisterMetaType<IPCAction>();
    qRegisterMetaTypeStreamOperators<IPCAction>();
    qRegisterMetaType<QList<IPCAction> >();
    qRegisterMetaTypeStreamOperators<QList<IPCAction> >();
    qRegisterMetaType<QUuid >();
    qRegisterMetaTypeStreamOperators<QUuid >();

    MRmiServer *s = new MRmiServer(".mabstractappdecorator", this);
    s->exportObject(this);
    d->remote_app = 0;
}

MAbstractAppInterface::~MAbstractAppInterface()
{
}

void MAbstractAppInterface::RemoteSetActions(QList<IPCAction> menu, uint window)
{
    //qCritical()<<__PRETTY_FUNCTION__<<menu.count()<<window;
    actionsChanged(menu, (WId)window);
}

void MAbstractAppInterface::RemoteSetClientKey(const QString& key)
{
    Q_D(MAbstractAppInterface);

    delete d->remote_app;

    d->remote_app = new MRmiClient(key, this);
}

void MAbstractAppInterface::triggered(QUuid id, bool val)
{
    Q_D(MAbstractAppInterface);

    if (d->remote_app)
        d->remote_app->invoke("QtMaemo6AppInterface", "triggered", QVariant::fromValue(id), val);
}

void MAbstractAppInterface::toggled(QUuid id, bool val)
{
    Q_D(MAbstractAppInterface);

    if (d->remote_app)
        d->remote_app->invoke("QtMaemo6AppInterface", "toggled", QVariant::fromValue(id), val);
}

