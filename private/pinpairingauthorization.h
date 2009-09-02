/*
 * Copyright 2009 by Rob Scheepmaker <r.scheepmaker@student.utwente.nl>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef PLASMA_PINPAIRINGAUTHORIZATION_H
#define PLASMA_PINPAIRINGAUTHORIZATION_H

#include "plasma_export.h"
#include "pinpairingauthorization.h"
#include "authorizationinterface.h"

#include <QtCore/QObject>

namespace Plasma
{

/**
 * @class PinPairingAuthorization plasma/pinpairingauthorization.h <Plasma/PinPairingAuthorization>
 *
 * @short Implementation of AuthorizationInterface that you can use if you want to use standard
 * pin pairing authorization (let the user type the same password at both sides) in your shell for
 * every rule that doesn't match.
 *
 * @since 4.4?
 */
class PLASMA_EXPORT PinPairingAuthorization : public AuthorizationInterface
{
    public:
        PinPairingAuthorization();
        ~PinPairingAuthorization();
        virtual void authorizationRequest(AuthorizationRule &rule);
        virtual void clientPinRequest(ClientPinRequest &request);
};
} // Plasma namespace

#endif

