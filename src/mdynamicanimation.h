/***************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
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

#ifndef MDYNAMICANIMATION_H
#define MDYNAMICANIMATION_H

class QPropertyAnimation;
class MStatusBarCrop;
class QAbstractAnimation;

#include <mcompositewindowanimation.h>
#include <QVector>

typedef QVector<QAbstractAnimation*> AnimVector;

class MDynamicAnimation: public MCompositeWindowAnimation
{
    Q_OBJECT
 public:
    MDynamicAnimation(QObject* parent = 0);
    ~MDynamicAnimation();
    void setEnabled(bool enabled);
    void disableAnimation( QAbstractAnimation * animation );
    
    AnimVector& activeAnimations();
 private:
    AnimVector animvec;
};

class MSheetAnimation: public MDynamicAnimation
{
    Q_OBJECT
 public:
    MSheetAnimation(QObject* parent = 0);

    virtual void windowShown(); 
    virtual void windowClosed();
};

class MChainedAnimation: public MDynamicAnimation
{
    Q_OBJECT
 public:
    MChainedAnimation(QObject* parent = 0);

    virtual void windowShown(); 
    virtual void windowClosed();

 private slots:
    void endAnimation();

 private:
    MCompositeWindow* invokerWindow();
    QPropertyAnimation* invoker_pos;
    MStatusBarCrop* cropper;
};

class MCallUiAnimation: public MDynamicAnimation
{
    Q_OBJECT
 public:
    enum CallMode {
        NoCall = 0,
        IncomingCall, 
        OutgoingCall 
    };

    MCallUiAnimation(QObject* parent = 0);

    virtual void windowShown(); 
    virtual void windowClosed();
    void setupBehindAnimation();
    void setupCallMode(bool showWindow = true);
        
 private slots:
    void endAnimation();
    void stackcallui();

 private:
    void tempHideDesktop(MCompositeWindow* behind);

    CallMode call_mode;    
    QPropertyAnimation* currentwin_pos;
    QPropertyAnimation* currentwin_scale;
    QPropertyAnimation* currentwin_opac;
    MStatusBarCrop* cropper;
    QPointer<MCompositeWindow> behindTarget;
};

//MDYNAMICANIMATION_H

#endif
