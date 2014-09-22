/******************************************************************************
 *  Copyright 2013 Sebastian Kügler <sebas@kde.org>                           *
 *                                                                            *
 *  This library is free software; you can redistribute it and/or             *
 *  modify it under the terms of the GNU Lesser General Public                *
 *                                                                            *
 *  License as published by the Free Software Foundation; either              *
 *  version 2.1 of the License, or (at your option) version 3, or any         *
 *  later version accepted by the membership of KDE e.V. (or its              *
 *  successor approved by the membership of KDE e.V.), which shall            *
 *  act as a proxy defined in Section 6 of version 3 of the license.          *
 *                                                                            *
 *  This library is distributed in the hope that it will be useful,           *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU         *
 *  Lesser General Public License for more details.                           *
 *                                                                            *
 *  You should have received a copy of the GNU Lesser General Public          *
 *  License along with this library.  If not, see                             *
 *  <http://www.gnu.org/licenses/>.                                           *
 *                                                                            *
 ******************************************************************************/

#include <qcommandlineparser.h>
#include <qcommandlineoption.h>

#include "kconfigtojson.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString description = QStringLiteral("Updates the cache of a package directory");
    const char version[] = "1.0";

    app.setApplicationVersion(version);

    const static QString _p = QStringLiteral("packagedir");
    const static QString _o = QStringLiteral("output");
    const static QString _n = QStringLiteral("name");

    QCommandLineOption packagedir = QCommandLineOption(QStringList() << QStringLiteral("p") << _p,
                               QStringLiteral("Generate cache of packagedir"), _n);
    QCommandLineOption output = QCommandLineOption(QStringList() << QStringLiteral("o") << _o,
                                QStringLiteral("Write output to file"), _n);

    QCommandLineParser parser;
    parser.addVersionOption();
    parser.setApplicationDescription(description);
    parser.addHelpOption();
    parser.addOption(packagedir);
    parser.addOption(output);

    KConfigToJson dtj(&parser, packagedir, output);

    parser.process(app);
    return dtj.runMain();
}
