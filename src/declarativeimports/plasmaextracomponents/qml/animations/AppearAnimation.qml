// -*- coding: iso-8859-1 -*-
/*
 *   Copyright 2011 Sebastian Kügler <sebas@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  2.010-1301, USA.
 */

import QtQuick 2.2

SequentialAnimation {
    id: appearAnimation
    objectName: "appearAnimation"

    property Item targetItem
    property int duration: units.longDuration

    // Animators run on the render thread so they kick in slightly delayed
    // so explicitly set the item's opacity to 0 before starting the animation
    ScriptAction {
        script: {
            targetItem.opacity = 0
        }
    }

    ParallelAnimation {
        OpacityAnimator {
            target: targetItem
            from: 0
            to: 1.0
            duration: appearAnimation.duration
            easing.type: Easing.InExpo
        }
        ScaleAnimator {
            target: targetItem
            from: 0.8
            to: 1.0
            duration: appearAnimation.duration
            easing.type: Easing.InExpo
        }
    }
}
