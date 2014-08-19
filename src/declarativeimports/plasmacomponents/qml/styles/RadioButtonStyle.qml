/*
 *   Copyright 2014 Marco Martin <mart@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Library General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  2.010-1301, USA.
 */

import QtQuick 2.0
import QtQuick.Controls.Styles 1.1 as QtQuickControlStyle

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 2.0 as PlasmaComponents


QtQuickControlStyle.RadioButtonStyle {
    id: radiobuttonStyle

    label: PlasmaComponents.Label {
        text: control.text
    }
    //Not needed?
    background: Item {}

    spacing: units.smallSpacing

    indicator: PlasmaCore.SvgItem {
        svg: PlasmaCore.Svg {
            id: buttonSvg
            imagePath: "widgets/actionbutton"
        }
        elementId: "normal"
        //slightly bigger then the checkbox: being round its apparent size is smaller
        //making it slightly bigger seems to make it more balanced giving
        //it roughly the same area
        width: units.gridUnit * 1.2
        height: width

        PlasmaCore.SvgItem {
            svg: PlasmaCore.Svg {
                id: checkmarkSvg
                imagePath: "widgets/checkmarks"
            }
            elementId: "radiobutton"
            opacity: control.checked ? 1 : 0
            anchors {
                fill: parent
            }
            Behavior on opacity {
                NumberAnimation {
                    duration: units.longDuration
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}