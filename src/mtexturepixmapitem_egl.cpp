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

#include "mtexturepixmapitem.h"
#include "mtexturepixmapitem_p.h"
#include "mcompositewindowgroup.h"

#include <QPainterPath>
#include <QRect>
#include <QGLContext>
#include <QX11Info>
#include <QVector>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>

//#define GL_GLEXT_PROTOTYPES

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = 0; 
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = 0; 
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = 0;
static EGLint attribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE }; 

class EglTextureManager
{
public:

    EglTextureManager() {
        genTextures(20);
    }

    ~EglTextureManager() {
        int sz = all_tex.size();
        glDeleteTextures(sz, all_tex.constData());
    }

    GLuint getTexture() {
        if (free_tex.empty())
            genTextures(10);
        GLuint ret = free_tex.back();
        free_tex.pop_back();
        return ret;
    }

    void closeTexture(GLuint texture) {
        // clear this texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, 0);
        free_tex.push_back(texture);
    }
private:

    void genTextures(int n) {
        GLuint tex[n + 1];
        glGenTextures(n, tex);
        for (int i = 0; i < n; i++) {
            free_tex.push_back(tex[i]);
            all_tex.push_back(tex[i]);
        }
    }

    QVector<GLuint> free_tex, all_tex;
};

class EglResourceManager
{
public:
    EglResourceManager()
        : has_tfp(false) {
        int maj, min;
        if (!dpy) {
            dpy = eglGetDisplay(EGLNativeDisplayType(QX11Info::display()));
            eglInitialize(dpy, &maj, &min);
        }

        QString exts = QLatin1String(eglQueryString(dpy, EGL_EXTENSIONS));
        if ((exts.contains("EGL_KHR_image") &&
             exts.contains("EGL_KHR_gl_texture_2D_image"))) {
            has_tfp = true;
            eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
            eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR"); 
            glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES"); 
        } else {
            qDebug("EGL version: %d.%d\n", maj, min);
            qDebug("No EGL tfp support.\n");
        }
        texman = new EglTextureManager();
    }

    bool texturePixmapSupport() {
        return has_tfp;
    }

    EglTextureManager *texman;
    static EGLConfig config;
    static EGLConfig configAlpha;
    static EGLDisplay dpy;

    bool has_tfp;
};

EglResourceManager *MTexturePixmapPrivate::eglresource = 0;
EGLConfig EglResourceManager::config = 0;
EGLConfig EglResourceManager::configAlpha = 0;
EGLDisplay EglResourceManager::dpy = 0;

void MTexturePixmapItem::init()
{
    if ((!isValid() && !propertyCache()->isVirtual())
        || propertyCache()->isInputOnly())
        return;
    
    if (!d->eglresource)
        d->eglresource = new EglResourceManager();

    d->custom_tfp = !d->eglresource->texturePixmapSupport();
    d->textureId = d->eglresource->texman->getTexture();
    glEnable(GL_TEXTURE_2D);
    
    if (d->custom_tfp)
        d->inverted_texture = false;
    
    glBindTexture(GL_TEXTURE_2D, d->textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    d->saveBackingStore();
    d->damageRetryTimer.setSingleShot(true);
    connect(&d->damageRetryTimer, SIGNAL(timeout()),
            SLOT(updateWindowPixmapProxy()));
}

MTexturePixmapItem::MTexturePixmapItem(Window window, MWindowPropertyCache *mpc,
                                       QGraphicsItem* parent)
    : MCompositeWindow(window, mpc, parent),
      d(new MTexturePixmapPrivate(window, this))
{
    init();
}

static void freeEglImage(MTexturePixmapPrivate *d)
{
    if (d->egl_image != EGL_NO_IMAGE_KHR) {
        /* Free EGLImage from the texture */
        glBindTexture(GL_TEXTURE_2D, d->textureId);
        /*
         * Texture size 64x64 is minimum required by GL. But we can assume 0x0
         * works with modern drivers/hw.
         */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);
        eglDestroyImageKHR(d->eglresource->dpy, d->egl_image);
        d->egl_image = EGL_NO_IMAGE_KHR;
    }
}

void MTexturePixmapItem::rebindPixmap()
{
    freeEglImage(d);
    if (!d->windowp) {
        d->egl_image = EGL_NO_IMAGE_KHR;
        return;
    }
    
    d->ctx->makeCurrent();
    doTFP();
}

void MTexturePixmapItem::enableDirectFbRendering()
{
    if (d->direct_fb_render || propertyCache()->isVirtual())
        return;
    if (propertyCache())
        propertyCache()->damageTracking(false);

    d->direct_fb_render = true;

    freeEglImage(d);
    if (d->windowp) {
        XFreePixmap(QX11Info::display(), d->windowp);
        d->windowp = 0;
    }
    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
}

void MTexturePixmapItem::enableRedirectedRendering()
{
    if (!d->direct_fb_render || propertyCache()->isVirtual())
        return;
    if (propertyCache())
        propertyCache()->damageTracking(true);

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
    saveBackingStore();
    updateWindowPixmap();
}

MTexturePixmapItem::~MTexturePixmapItem()
{
    cleanup();
    delete d; // frees the pixmap too
}

void MTexturePixmapItem::initCustomTfp()
{
    // UNUSED. 
    // TODO: GLX backend should probably use same approach as here and
    // re-use same texture id
}

void MTexturePixmapItem::cleanup()
{
    freeEglImage(d);
    d->eglresource->texman->closeTexture(d->textureId);
}

void MTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num,
                                            Time when)
{
    // When a window is in transitioning limit the number of updates
    // to @limit/@expiry miliseconds.
    const unsigned expiry = 1000;
    const int      limit  =   30;

    if (hasTransitioningWindow()) {
        // Limit the number of damages we're willing to process if we're
        // in the middle of a transition, so the competition for the GL
        // resources will be less tight.
        if (d->pastDamages) {
            // Forget about pastDamages we received long ago.
            while (d->pastDamages->size() > 0
                   && d->pastDamages->first() + expiry < when)
                d->pastDamages->removeFirst();
            if (d->pastDamages->size() >= limit) {
                // Too many damages in the given timeframe, postpone
                // until the time the queue is ready to accept a new
                // update.
                if (!d->damageRetryTimer.isActive()) {
                    d->damageRetryTimer.setInterval(
                               d->pastDamages->first()+expiry - when);
                    d->damageRetryTimer.start();
                }
                return;
            }
        } else
            d->pastDamages = new QList<Time>;
        // Can afford this damage, but record when we received it,
        // so to know when to forget about them.
        d->pastDamages->append(when);
    } else if (d->pastDamages) {
        // The window is not transitioning, forget about all pastDamages.
        delete d->pastDamages;
        d->pastDamages = NULL;
    }
    d->damageRetryTimer.stop();

    // we want to update the pixmap even if the item is not visible because
    // certain animations require up-to-date pixmap (alternatively we could mark
    // it dirty and update it before the animation starts...)
    if (d->direct_fb_render || propertyCache()->isInputOnly())
        return;

    if (!rects)
        // no rects means the whole area
        d->damageRegion = boundingRect().toRect();
    else {
        QRegion r;
        for (int i = 0; i < num; ++i)
             r += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
        d->damageRegion = r;
    }
    
    bool new_image = false;
    if (d->custom_tfp) {
        QPixmap qp = QPixmap::fromX11Pixmap(d->windowp);
        
        QImage img = d->glwidget->convertToGLFormat(qp.toImage());
        glBindTexture(GL_TEXTURE_2D, d->textureId);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width(), 
                        img.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        new_image = true;
    } else if (d->egl_image == EGL_NO_IMAGE_KHR) {
        saveBackingStore();
        new_image = true;
    }    
    if (new_image || !d->damageRegion.isEmpty()) {
        if (!d->current_window_group) 
            d->glwidget->update();
        else
            d->current_window_group->updateWindowPixmap();
    }
}

void MTexturePixmapItem::doTFP()
{
    if (isClosing()) // Pixmap is already freed. No sense to create EGL image
        return;      // from it again

    if (d->custom_tfp) {
        // no EGL texture from pixmap extensions available
        // use regular X11/GL calls to copy pixels from Pixmap to GL Texture
        QPixmap qp = QPixmap::fromX11Pixmap(d->windowp);

        QT_TRY {
            QImage img = QGLWidget::convertToGLFormat(qp.toImage());
            glBindTexture(GL_TEXTURE_2D, d->textureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        } QT_CATCH(std::bad_alloc e) {
            /* XGetImage() failed, the window has been unmapped. */;
            qWarning("MTexturePixmapItem::%s(): std::bad_alloc e", __func__);
        }
    } else { //use EGL extensions
        d->egl_image = eglCreateImageKHR(d->eglresource->dpy, 0,
                                         EGL_NATIVE_PIXMAP_KHR,
                                         (EGLClientBuffer)d->windowp,
                                         attribs);
        if (d->egl_image == EGL_NO_IMAGE_KHR) {
            // window is probably unmapped
            /*qWarning("MTexturePixmapItem::%s(): Cannot create EGL image: 0x%x",
                     __func__, eglGetError());*/
            return;
        } else {
            glBindTexture(GL_TEXTURE_2D, d->textureId);
            glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, d->egl_image);
        }
    }
}
