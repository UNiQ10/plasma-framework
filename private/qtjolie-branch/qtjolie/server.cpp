/**
  * This file is part of the KDE project
  * Copyright (C) 2009 Kevin Ottens <ervin@kde.org>
  *
  * This library is free software; you can redistribute it and/or
  * modify it under the terms of the GNU Library General Public
  * License as published by the Free Software Foundation; either
  * version 2 of the License, or (at your option) any later version.
  *
  * This library is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * Library General Public License for more details.
  *
  * You should have received a copy of the GNU Library General Public License
  * along with this library; see the file COPYING.LIB.  If not, write to
  * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  * Boston, MA 02110-1301, USA.
  */

#include "server.h"
#include "server_p.h"

#include <QtCore/QDebug>
#include <QtCore/QString>

#include "abstractadaptor.h"
#include "serverthread_p.h"
#include "message.h"
#include "pendingcall.h"
#include "pendingreply.h"

#define QT_NO_CAST_FROM_ASCII
#define QT_NO_CAST_TO_ASCII

using namespace Jolie;

Server::Server(quint16 port)
    : d(new ServerPrivate(this))
{
    d->serverThread = new ServerThread(port, d);
    d->serverThread->start();
}

Server::~Server()
{
    delete d->serverThread;
    delete d;
}

Server::Error Server::error() const
{
    return d->error;
}

QString Server::errorString() const
{
    return d->errorString;
}

bool Server::registerAdaptor(const QByteArray &path, AbstractAdaptor *adaptor)
{
    //qDebug() << "registering an adaptor: " << QString(path);
    if (path.isEmpty() || d->adaptors.contains(path)) {
        return false;
    }

    d->adaptors[path] = adaptor;
    return true;
}

bool Server::unregisterAdaptor(const QByteArray &path)
{
    return d->adaptors.take(path)!=0;
}

void Server::sendMessage(int descriptor, const Message &message)
{
    d->serverThread->sendMessage(descriptor, message);
}

void ServerPrivate::messageReceived(int descriptor, const Message &message)
{
    if (adaptors.contains(message.resourcePath())) {
        adaptors[message.resourcePath()]->relay(q, descriptor, message);
        //serverThread->sendMessage(descriptor, reply);
    } else {
        qWarning() << "Got a message for an unregistered object:"
                   << message.operationName()
                   << "@"
                   << message.resourcePath();
    }
}

