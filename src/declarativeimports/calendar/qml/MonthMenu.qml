/*
 *  Copyright 2013 Heena Mahour <heena393@gmail.com>
 *  Copyright 2013 Martin Klapetek <mklapetek@kde.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  2.010-1301, USA.
 */

import QtQuick 2.2
import org.kde.plasma.components 2.0 as PlasmaComponents

PlasmaComponents.Menu {
    id: testMenu
    property int year

    // Needs to be a property since Menu doesn't accept other items than MenuItem
    property Instantiator items: Instantiator {
        model: 12
        PlasmaComponents.MenuItem {
            text: capitalizeFirstLetter(Qt.locale().standaloneMonthName(index))
            onClicked: calendarBackend.displayedDate = new Date(year, index, 1)
        }
        onObjectAdded: testMenu.addMenuItem(object)
    }

    // Because some locales don't have it in standaloneMonthNames,
    // but we want our GUI to be pretty and want capitalization always
    function capitalizeFirstLetter(monthName) {
        return monthName.charAt(0).toUpperCase() + monthName.slice(1);
    }
}
