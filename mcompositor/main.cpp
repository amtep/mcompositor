/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
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

#include <QtGui>
#include <QGLWidget>
#include "mcompositescene.h"
#include "mcompositemanager.h"

#define TMPDIR "/tmp"
#define HACKDIR ".mcompositor-qt-plugin-hack"

int main(int argc, char *argv[])
{
    // We don't need meego graphics system
    setenv("QT_GRAPHICSSYSTEM", "raster", 1);

#ifdef WINDOW_DEBUG
    // React to context-commander's fake events; required by test20.py
    // to be able to fake a phone call.
    setenv("CONTEXT_COMMANDING", "1", 1);
#endif
    
    // Load only Qt plugins that are for image formats. This seems to be
    // possible only with some hackery (otherwise we load e.g. style plugins).
    QStringList oldpath = QCoreApplication::libraryPaths();
    if (!oldpath.size())
        oldpath.append(QString("/usr/lib/qt4/plugins"));
    QDir tmpdir(QString(TMPDIR));
    tmpdir.mkdir(QString(HACKDIR));
    bool succ = tmpdir.cd(QString(HACKDIR));
    tmpdir.mkdir(QString("plugins"));
    succ = succ && tmpdir.cd(QString("plugins"));
    QFile fmts(QString(TMPDIR "/" HACKDIR "/plugins/imageformats"));
    fmts.remove();
    fmts.setFileName(oldpath[0] + QString("/imageformats"));
    succ = succ && fmts.link(QString(TMPDIR "/" HACKDIR
                                     "/plugins/imageformats"));
    if (succ)
        QCoreApplication::setLibraryPaths(
                             QStringList(TMPDIR "/" HACKDIR "/plugins"));
    else
        QCoreApplication::setLibraryPaths(QStringList());

    QApplication::setDesktopSettingsAware(false);
    // make QPixmap create X pixmaps
    QApplication::setGraphicsSystem("native");
    MCompositeManager app(argc, argv);

    QGraphicsScene *scene = app.scene();
    QGraphicsView view(scene);
    
    view.setProperty("NoMStyle", true);
    view.setUpdatesEnabled(false);
    view.setAutoFillBackground(false);
    view.setBackgroundBrush(Qt::NoBrush);
    view.setForegroundBrush(Qt::NoBrush);
    view.setFrameShadow(QFrame::Plain);

    view.setWindowFlags(Qt::X11BypassWindowManagerHint);
    view.setAttribute(Qt::WA_NoSystemBackground);
    view.move(-2, -2);
    view.setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
    view.setOptimizationFlags(QGraphicsView::IndirectPainting);
    app.setSurfaceWindow(view.winId());

    view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    view.setMinimumSize(QApplication::desktop()->width() + 2,
                        QApplication::desktop()->height() + 2);
    view.setMaximumSize(QApplication::desktop()->width() + 2,
                        QApplication::desktop()->height() + 2);

    QGLFormat fmt;
    fmt.setSamples(0);
    fmt.setSampleBuffers(false);

    QGLWidget *w = new QGLWidget(fmt);
    w->setAttribute(Qt::WA_PaintOutsidePaintEvent);
    if (!app.isEgl()) {
        QPalette p = w->palette();
        p.setColor(QPalette::Background, QColor(Qt::black));
        w->setPalette(p);
        w->update();
    }
    w->setAutoFillBackground(false);
    w->setMinimumSize(QApplication::desktop()->width(),
                      QApplication::desktop()->height());
    w->setMaximumSize(QApplication::desktop()->width(),
                      QApplication::desktop()->height());
    app.setGLWidget(w);
    view.setViewport(w);
    w->makeCurrent();
    view.show();

    // The directory is hard-coded for now. could be moved this
    // to $plugindir later.
    int testPlugin;
    const QStringList &args = app.arguments();
    for (testPlugin = 1; testPlugin < args.length(); testPlugin++)
        if (!args[testPlugin].isEmpty() && args[testPlugin][0] != '-')
            break;
    app.loadPlugins(testPlugin < args.length() ? args[testPlugin] : QString(),
                    "/usr/lib/mcompositor");

    app.prepareEvents();
    app.redirectWindows();
    return app.exec();
}
