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

#include "mcompositewindow.h"
#include "mcompositewindowanimation.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mtexturepixmapitem.h"
#include "mdecoratorframe.h"
#include "mcompositemanagerextension.h"
#include "mcompositewindowgroup.h"
#include "msplashscreen.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <X11/Xatom.h>

int MCompositeWindow::window_transitioning = 0;

MCompositeWindow::MCompositeWindow(Qt::HANDLE window, 
                                   MWindowPropertyCache *mpc, 
                                   QGraphicsItem *p)
    : QGraphicsItem(p),
      pc(mpc),
      animator(0),
      zval(1),
      sent_ping_timestamp(0),
      received_ping_timestamp(0),
      blur(false),
      iconified(false),
      iconified_final(false),
      iconify_state(NoIconifyState),
      destroyed(false),
      window_status(Normal),
      need_decor(false),
      window_obscured(-1),
      is_transitioning(false),
      dimmed_effect(false),
      waiting_for_damage(0),
      resize_expected(false),
      win_id(window)
{
    if (!mpc || (mpc && !mpc->is_valid && !mpc->isVirtual())) {
        is_valid = false;
        newly_mapped = false;
        t_ping = t_reappear = damage_timer = 0;
        window_visible = false;
        return;
    } else
        is_valid = true;
    connect(mpc, SIGNAL(iconGeometryUpdated()), SLOT(updateIconGeometry()));

    // this could be configurable. But will do for now. Most WMs use 5s delay
    t_ping = new QTimer(this);
    t_ping->setInterval(5000);
    connect(t_ping, SIGNAL(timeout()), SLOT(pingTimeout()));
    t_reappear = new QTimer(this);
    t_reappear->setSingleShot(true);
    t_reappear->setInterval(30 * 1000);
    connect(t_reappear, SIGNAL(timeout()), SLOT(reappearTimeout()));

    damage_timer = new QTimer(this);
    damage_timer->setSingleShot(true);
    connect(damage_timer, SIGNAL(timeout()), SLOT(damageReceived()));

    // Newly-mapped non-decorated application windows are not initially 
    // visible to prevent flickering when animation is started.
    // We initially prevent item visibility from compositor itself
    // or it's corresponding thumbnail rendered by the switcher
    bool is_app = isAppWindow();
    newly_mapped = is_app;
    if (!pc->isInputOnly()) {
        // never paint InputOnly windows
        window_visible = !is_app;
        setVisible(window_visible); // newly_mapped used here
    } else
        window_visible = false;

    MCompositeWindowAnimation* a = new MCompositeWindowAnimation(this);
    a->setTargetWindow(this);
}

MCompositeWindow::~MCompositeWindow()
{
    MCompositeManager *p = (MCompositeManager *) qApp;

    endAnimation();    
    if (pc) {
        pc->damageTracking(false);
        p->d->prop_caches.remove(window());
        pc->deleteLater();
    }
    if (animator)
        delete animator;
}

void MCompositeWindow::setBlurred(bool b)
{
    blur = b;
    update();
}

bool MCompositeWindow::blurred()
{
    return blur;
}

/* This is a delayed animation. Actual animation is triggered
 * when startTransition() is called. Returns true if signal will come.
 */
bool MCompositeWindow::iconify(bool defer)
{
    if (iconify_state == ManualIconifyState) {
        setIconified(true);
        window_status = Normal;
        return false;
    }

    if (window_status != MCompositeWindow::Closing)
        window_status = MCompositeWindow::Minimizing;
    
    iconified = true;
    
    // iconify handler
    if (animator) {
        if (defer)
            animator->deferAnimation(MCompositeWindowAnimation::Iconify);
        else
            animator->windowIconified();
        window_status = Normal;
        return true;
    }
    return false;
}

void MCompositeWindow::setUntransformed()
{
    endAnimation();
    
    newly_mapped = false;
    setVisible(true);
    setOpacity(1.0);
    setScale(1.0);
    iconified = false;
}

void MCompositeWindow::setIconified(bool iconified)
{
    iconified_final = iconified;
    iconify_state = ManualIconifyState;
    if (iconified && !animator->pendingAnimation())
        emit itemIconified(this);
    else if (!iconified && !animator->pendingAnimation())
        iconify_state = NoIconifyState;
}

MCompositeWindow::IconifyState MCompositeWindow::iconifyState() const
{
    return iconify_state;
}

void MCompositeWindow::setIconifyState(MCompositeWindow::IconifyState state)
{
    iconify_state = state;
}

void MCompositeWindow::setWindowObscured(bool obscured, bool no_notify)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    short new_value = obscured ? 1 : 0;
    if ((new_value == window_obscured && !newly_mapped)
        || (!obscured && p->displayOff() && !pc->lowPowerMode()))
        return;
    window_obscured = new_value;

    if (!no_notify && !pc->isVirtual()) {
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = window();
        c.state      = obscured ? VisibilityFullyObscured :
                                  VisibilityUnobscured;
        XSendEvent(QX11Info::display(), window(), true,
                   VisibilityChangeMask, (XEvent *)&c);
    }
}

/*
 * We ensure that ensure there are ALWAYS updated thumbnails in the 
 * switcher by letting switcher know in advance of the presence of this window.
 * Delay the minimize animation until we receive an iconGeometry update from
 * the switcher
 */
void MCompositeWindow::startTransition()
{
    if (iconified && pc->iconGeometry().isNull())
        return;
    if (animator && animator->pendingAnimation()) {
        // don't trigger irrelevant windows
        // if (animator->targetWindow() != this)
        //     animator->setTargetWindow(this);
        MCompositeWindow::setVisible(true);
        animator->startTransition();
    }
}

void MCompositeWindow::updateIconGeometry()
{
    if (pc && pc->iconGeometry().isNull())
        return;

    // trigger transition the second time around and update animation values
    if (iconified) 
        startTransition();
}

// TODO: have an option of disabling the animation
void MCompositeWindow::restore(bool defer)
{
    setVisible(true);
    iconified = false;
     // Restore handler
    if (animator) {
        if (defer)
            animator->deferAnimation(MCompositeWindowAnimation::Restore);
        else
            animator->windowRestored();
    }
}

bool MCompositeWindow::showWindow()
{
    if (type() == MSplashScreen::Type) {
        q_fadeIn();
        return true;
    }
    // defer putting this window in the _NET_CLIENT_LIST
    // only after animation is done to prevent the switcher from rendering it
    if (!isAppWindow() || !pc || !pc->is_valid
        // isAppWindow() returns true for system dialogs
        || pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG))
        return false;
    
    findBehindWindow();
    beginAnimation();
    if (newly_mapped) {
        // NB#180628 - some stupid apps are listening for visibilitynotifies.
        // Well, all of the toolkit anyways
        setWindowObscured(false);
        // waiting for two damage events seems to work for Meegotouch apps
        // at least, for the rest, there is a timeout
        waiting_for_damage = 2;
        resize_expected = false;
        damage_timer->setInterval(500);
        damage_timer->start();
    } else
        q_fadeIn();
    return true;
}

void MCompositeWindow::expectResize()
{
    // In addition to @waiting_for_damages, also wait for a resize().
    // Be nice and wait some more because mdecorator has a huge latency.
    if (!damage_timer->isActive())
        return;
    resize_expected = true;
    damage_timer->setInterval(800);
}

void MCompositeWindow::damageReceived()
{
    if (!waiting_for_damage && !resize_expected) {
        // We aren't planning to show the window.
        Q_ASSERT(!damage_timer->isActive());
        return;
    } else if (damage_timer->isActive()) {
        // We're within timeout and just got a damage.
        Q_ASSERT(waiting_for_damage > 0);
        if (--waiting_for_damage || resize_expected)
            // Conditions haven't been met yet.
            return;
    }

    // Either timeout or the conditions have been met.
    Q_ASSERT(!damage_timer->isActive() ||
             (!waiting_for_damage && !resize_expected));
    damage_timer->stop();
    waiting_for_damage = 0;
    resize_expected = false;

    // We're ready to take over the splash screen.
    MCompositeWindow *splash;
    splash = static_cast<MCompositeManager *>(qApp)->splashed(this);
    if (splash) {
        splash->startTransition();
        newly_mapped = false;
    } else
        q_fadeIn();
}

void MCompositeWindow::resize(int, int)
{
    if (!resize_expected)
        return;
    if (!waiting_for_damage) {
        // We got the expected resize and the damages have arrived too.
        // Simulate a timeout to kick the animation.
        damage_timer->stop();
        damageReceived();
    } else
        resize_expected = false;
}

void MCompositeWindow::q_fadeIn()
{   
    endAnimation();
    
    newly_mapped = false;
    setVisible(true);
    setOpacity(0.0);
    updateWindowPixmap();
    newly_mapped = true;
    
    iconified = false;
    // fade-in handler
    if (animator) {
        // always ensure the animation is visible. zvalues get corrected later 
        // at checkStacking 
        setZValue(((MCompositeManager *) qApp)->d->stacking_list.size()+1);
        animator->windowShown();
    }
}

void MCompositeWindow::closeWindowRequest()
{
    if (!pc || !pc->is_valid || (!isMapped() && !pc->beingMapped()))
        return;
    if (!windowPixmap() && !pc->isInputOnly()) {
        // get a Pixmap for the possible unmap animation
        MCompositeManager *p = (MCompositeManager *) qApp;
        if (!p->isCompositing())
            p->d->enableCompositing();
        updateWindowPixmap();
    }
    emit closeWindowRequest(this);
}

void MCompositeWindow::closeWindowAnimation()
{
    if (!pc || !pc->is_valid || window_status == Closing
        || pc->isInputOnly() || pc->isOverrideRedirect()
        || !windowPixmap() || !isAppWindow()
        // isAppWindow() returns true for system dialogs
        || pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG)
        || propertyCache()->windowState() == IconicState
        || window_status == MCompositeWindow::Hung) {
        return;
    }
    window_status = Closing; // animating, do not disturb
    
    MCompositeManager *p = (MCompositeManager *) qApp;
    bool defer = false;
    setVisible(true);
    if (!p->isCompositing()) {
        p->d->enableCompositing();
        defer = true;
    }
    
    // fade-out handler
    if (animator) {
        if (defer)
            animator->deferAnimation(MCompositeWindowAnimation::Closing);
        else
            animator->windowClosed();
        window_status = Normal;
    }
}

bool MCompositeWindow::event(QEvent *e)
{
    if (e->type() == QEvent::DeferredDelete && is_transitioning) {
        // Can't delete the object yet, try again in the next iteration.
        deleteLater();
        return true;
    } else
        return QObject::event(e);
}

void MCompositeWindow::finalizeState()
{
    // as far as this window is concerned, it's OK to direct render
    window_status = Normal;

    // iconification status
    if (iconified) {
        iconified = false;
        iconified_final = true;
        hide();
        iconify_state = TransitionIconifyState;
        emit itemIconified(this);
    } else {
        iconify_state = NoIconifyState;
        iconified_final = false;
        show();
        // no delay: window does not need to be repainted when restoring
        // from the switcher (even then the animation should take long enough
        // to allow it)
        q_itemRestored();
    }
    
    // item lifetime
    if (destroyed)
        deleteLater();
}

void MCompositeWindow::q_itemRestored()
{
    emit itemRestored(this);
}

void MCompositeWindow::requestZValue(int zvalue)
{
    // when animating, Z-value is set again after finishing the animation
    // (setting it later in finalizeState() caused flickering)
    if (animator && !animator->isActive() && !animator->pendingAnimation())
        setZValue(zvalue);
}

bool MCompositeWindow::isIconified() const
{
    if (animator->isActive())
        return false;

    return iconified_final;
}

void MCompositeWindow::setVisible(bool visible)
{
    if ((pc && pc->isInputOnly())
        || (visible && newly_mapped && isAppWindow()) 
        || (!visible && is_transitioning)) 
        return;

    // Set the iconification status as well
    iconified_final = !visible;
    window_visible = visible;

    QGraphicsItem::setVisible(visible);
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->setWindowDebugProperties(window());

    QGraphicsScene* sc = scene();    
    if (sc && !visible && sc->items().count() == 1)
        clearTexture();
}

void MCompositeWindow::startPing(bool restart)
{
    if (restart)
        stopPing();
    else if (t_ping->isActive())
        // this function can be called repeatedly without extending the timeout
        return;
    // startup: send ping now, otherwise it is sent after timeout
    pingWindow(restart);
    t_ping->start();
}

void MCompositeWindow::stopPing()
{
    if (t_ping) t_ping->stop();
    if (t_reappear) t_reappear->stop();
}

void MCompositeWindow::startDialogReappearTimer()
{
    if (window_status != Hung)
        return;
    t_reappear->start();
}

void MCompositeWindow::reappearTimeout()
{
    if (window_status == Hung)
        // show "application not responding" UI again
        emit windowHung(this, true);
}

void MCompositeWindow::receivedPing(ulong serverTimeStamp)
{
    received_ping_timestamp = serverTimeStamp;
    
    if (window_status == Hung) {
        window_status = Normal;
        emit windowHung(this, false);
    }
    if (blurred())
        setBlurred(false);
    t_reappear->stop();
}

void MCompositeWindow::pingTimeout()
{
    if (received_ping_timestamp < sent_ping_timestamp
        && pc && pc->isMapped() && window_status != Hung
        && window_status != Minimizing && window_status != Closing) {
        window_status = Hung;
        emit windowHung(this, true);
    }
    if (t_ping->isActive())
        // interval timer is still active
        pingWindow();
}

void MCompositeWindow::pingWindow(bool restart)
{
    if (window_status == Hung && !restart)
        // don't send a new ping before the window responds, otherwise we may
        // queue up too many of them
        return;
    // It takes 5*4294967295s or 248551.35 days before it overflows.
    // Don't worry, you're not even remotely in danger on this platform.
    ulong t = ++sent_ping_timestamp;
    Window w = window();

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = ATOM(WM_PROTOCOLS);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ATOM(_NET_WM_PING);
    ev.xclient.data.l[1] = t;
    ev.xclient.data.l[2] = w;

    XSendEvent(QX11Info::display(), w, False, NoEventMask, &ev);
}

MCompositeWindow::WindowStatus MCompositeWindow::status() const
{
    return window_status;
}

bool MCompositeWindow::needDecoration() const
{
    return need_decor;
}

bool MCompositeWindow::needsCompositing() const
{
    if (!pc || (!pc->is_valid && !pc->isVirtual()))
        return false;
    if (need_decor || (pc->isDecorator() && !pc->opaqueWindow()))
        return true;
    if (pc->hasAlphaAndIsNotOpaque() && !pc->lowPowerMode())
        return true;
    return false;
}

void MCompositeWindow::setDecorated(bool decorated)
{
    need_decor = decorated;
}

MCompositeWindow *MCompositeWindow::compositeWindow(Qt::HANDLE window)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->windows.value(window, 0);
}

void MCompositeWindow::beginAnimation()
{
    if (!isMapped() && window_status != Closing)
        return;

    if (!is_transitioning) {
        ++window_transitioning;        
        is_transitioning = true;
    }
}

void MCompositeWindow::endAnimation()
{    
    if (is_transitioning) {
        is_transitioning = false;
        --window_transitioning;
        if (!window_transitioning)
            emit lastAnimationFinished(this);
    }
}

bool MCompositeWindow::hasTransitioningWindow()
{
    return window_transitioning > 0;
}

QVariant MCompositeWindow::itemChange(GraphicsItemChange change, const QVariant &value)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    if (change == ItemZValueHasChanged) {
        findBehindWindow();
        p->d->setWindowDebugProperties(window());
    }

    if (change == ItemVisibleHasChanged) {
        // Be careful not to update if this item whose visibility is about
        // to change is behind a visible item, to not reopen NB#189519.
        // Update is needed if visibility changes for a visible item
        // (other visible items get redrawn also).  Case requiring this:
        // status menu closed on top of an mdecorated window.
        bool ok_to_update = true;
        if (scene()) {
            QList<QGraphicsItem*> l = scene()->items();
            for (QList<QGraphicsItem*>::const_iterator i = l.begin();
                 i != l.end(); ++i)
                if ((*i)->isVisible()) {
                    ok_to_update = zValue() >= (*i)->zValue();
                    break;
                }
        }

        if (ok_to_update)
            // Nothing is visible or the topmost visible item is lower than us.
            p->d->glwidget->update();
    }

    return QGraphicsItem::itemChange(change, value);
}

void MCompositeWindow::findBehindWindow()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    for (int behind_i = indexInStack() - 1; behind_i >= 0; --behind_i) {
        Window behind_w = p->d->stacking_list.at(behind_i);
        MCompositeWindow* w = MCompositeWindow::compositeWindow(behind_w);
        if (!w) continue;
        if (w->propertyCache()->windowState() == NormalState 
            && w->propertyCache()->isMapped()
            && !w->propertyCache()->isDecorator()) 
            behind_window = w;
        else if (w->propertyCache()->isDecorator() &&
                 MDecoratorFrame::instance()->managedClient())
            behind_window = MDecoratorFrame::instance()->managedClient();
        else
            behind_window = MCompositeWindow::compositeWindow(p->d->stack[DESKTOP_LAYER]);
        break;
    }
}

void MCompositeWindow::update()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->glwidget->update();
}

bool MCompositeWindow::windowVisible() const
{
    return window_visible;
}

bool MCompositeWindow::isAppWindow(bool include_transients)
{
    if (pc && (pc->is_valid || pc->isVirtual()))
        return pc->isAppWindow(include_transients);
    else
        return false;
}

QPainterPath MCompositeWindow::shape() const
{    
    QPainterPath path;
    const QRegion &shape = propertyCache()->shapeRegion();
    if (QRegion(boundingRect().toRect()).subtracted(shape).isEmpty())
        path.addRect(boundingRect());
    else
        path.addRegion(shape);
    return path;
}

Window MCompositeWindow::lastVisibleParent() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    if (pc && pc->is_valid)
        return p->d->getLastVisibleParent(pc);
    else
        return None;
}

int MCompositeWindow::indexInStack() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->stacking_list.indexOf(window());
}

void MCompositeWindow::setIsMapped(bool mapped) 
{ 
    if (mapped)
        window_status = Normal; // make sure Closing -> Normal when remapped
    if (pc) pc->setIsMapped(mapped); 
}

bool MCompositeWindow::isMapped() const 
{
    return pc ? pc->isMapped() : false;
}

MCompositeWindowGroup* MCompositeWindow::group() const
{
#ifdef GLES2_VERSION
    return renderer()->current_window_group;
#else
    return 0;
#endif
}

MCompositeWindowAnimation* MCompositeWindow::windowAnimator() const
{
    return animator;
}
