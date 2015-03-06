/*
 *   Copyright 2006-2007 Aaron Seigo <aseigo@kde.org>
 *   Copyright 2008-2010 Marco Martin <notmart@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "svg.h"
#include "private/svg_p.h"
#include "private/theme_p.h"

#include <cmath>

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QMatrix>
#include <QPainter>
#include <QStringBuilder>

#include <kcolorscheme.h>
#include <kconfiggroup.h>
#include <QDebug>
#include <kfilterdev.h>
#include <kiconeffect.h>

#include "applet.h"
#include "package.h"
#include "theme.h"

namespace Plasma
{

SharedSvgRenderer::SharedSvgRenderer(QObject *parent)
    : QSvgRenderer(parent)
{
}

SharedSvgRenderer::SharedSvgRenderer(
    const QString &filename,
    const QString &styleSheet,
    QHash<QString, QRectF> &interestingElements,
    QObject *parent)
    : QSvgRenderer(parent)
{
    KCompressionDevice file(filename, KCompressionDevice::GZip);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    load(file.readAll(), styleSheet, interestingElements);
}

SharedSvgRenderer::SharedSvgRenderer(
    const QByteArray &contents,
    const QString &styleSheet,
    QHash<QString, QRectF> &interestingElements,
    QObject *parent)
    : QSvgRenderer(parent)
{
    load(contents, styleSheet, interestingElements);
}

bool SharedSvgRenderer::load(
    const QByteArray &contents,
    const QString &styleSheet,
    QHash<QString, QRectF> &interestingElements)
{
    // Apply the style sheet.
    if (!styleSheet.isEmpty() && contents.contains("current-color-scheme")) {
        QDomDocument svg;
        if (!svg.setContent(contents)) {
            return false;
        }

        QDomNode defs = svg.elementsByTagName("defs").item(0);

        for (QDomElement style = defs.firstChildElement("style"); !style.isNull();
                style = style.nextSiblingElement("style")) {
            if (style.attribute("id") == "current-color-scheme") {
                QDomElement colorScheme = svg.createElement("style");
                colorScheme.setAttribute("type", "text/css");
                colorScheme.setAttribute("id", "current-color-scheme");
                defs.replaceChild(colorScheme, style);
                colorScheme.appendChild(svg.createCDATASection(styleSheet));

                interestingElements.insert("current-color-scheme", QRect(0, 0, 1, 1));

                break;
            }
        }
        if (!QSvgRenderer::load(svg.toByteArray(-1))) {
            return false;
        }
    } else if (!QSvgRenderer::load(contents)) {
        return false;
    }

    // Search the SVG to find and store all ids that contain size hints.
    const QString contentsAsString(QString::fromLatin1(contents));
    QRegExp idExpr("id\\s*=\\s*(['\"])(\\d+-\\d+-.*)\\1");
    idExpr.setMinimal(true);

    int pos = 0;
    while ((pos = idExpr.indexIn(contentsAsString, pos)) != -1) {
        QString elementId = idExpr.cap(2);

        QRectF elementRect = boundsOnElement(elementId);
        if (elementRect.isValid()) {
            interestingElements.insert(elementId, elementRect);
        }

        pos += idExpr.matchedLength();
    }

    return true;
}

#define QLSEP QLatin1Char('_')
#define CACHE_ID_WITH_SIZE(size, id, devicePixelRatio) QString::number(int(size.width())) % QLSEP % QString::number(int(size.height())) % QLSEP % id % QLSEP % QLSEP % QString::number(int(devicePixelRatio))
#define CACHE_ID_NATURAL_SIZE(id, devicePixelRatio) QLatin1Literal("Natural") % QLSEP % id % QLSEP % QLSEP % QString::number(int(devicePixelRatio))

SvgPrivate::SvgPrivate(Svg *svg)
    : q(svg),
      renderer(0),
      styleCrc(0),
      colorGroup(Plasma::Theme::NormalColorGroup),
      lastModified(0),
      devicePixelRatio(1.0),
      scaleFactor(1.0),
      multipleImages(false),
      themed(false),
      fromCurrentTheme(false),
      applyColors(false),
      usesColors(false),
      cacheRendering(true),
      themeFailed(false)
{
}

SvgPrivate::~SvgPrivate()
{
    eraseRenderer();
}

//This function is meant for the rects cache
QString SvgPrivate::cacheId(const QString &elementId)
{
    if (size.isValid() && size != naturalSize) {
        return CACHE_ID_WITH_SIZE(size, elementId, devicePixelRatio);
    } else {
        return CACHE_ID_NATURAL_SIZE(elementId, devicePixelRatio);
    }
}

//This function is meant for the pixmap cache
QString SvgPrivate::cachePath(const QString &path, const QSize &size)
{
    return CACHE_ID_WITH_SIZE(size, path, devicePixelRatio) % QLSEP % QString::number(colorGroup);
}

bool SvgPrivate::setImagePath(const QString &imagePath)
{
    QString actualPath = imagePath;
    if (imagePath.startsWith("file://")) {
        //length of file://
        actualPath = actualPath.mid(7);
    }
    const bool isThemed = !QDir::isAbsolutePath(actualPath);

    // lets check to see if we're already set to this file
    if (isThemed == themed &&
            ((themed && themePath == actualPath) ||
             (!themed && path == actualPath))) {
        return false;
    }

    eraseRenderer();

    // if we don't have any path right now and are going to set one,
    // then lets not schedule a repaint because we are just initializing!
    bool updateNeeded = true; //!path.isEmpty() || !themePath.isEmpty();

    QObject::disconnect(actualTheme(), SIGNAL(themeChanged()), q, SLOT(themeChanged()));
    if (isThemed && !themed && s_systemColorsCache) {
        // catch the case where we weren't themed, but now we are, and the colors cache was set up
        // ensure we are not connected to that theme previously
        QObject::disconnect(s_systemColorsCache.data(), 0, q, 0);
    }

    themed = isThemed;
    path.clear();
    themePath.clear();
    localRectCache.clear();
    elementsWithSizeHints.clear();
    bool oldFromCurrentTheme = fromCurrentTheme;
    fromCurrentTheme = actualTheme()->currentThemeHasImage(imagePath);

    if (fromCurrentTheme != oldFromCurrentTheme) {
        emit q->fromCurrentThemeChanged(fromCurrentTheme);
    }

    if (themed) {
        themePath = actualPath;
        path = actualTheme()->imagePath(themePath);
        themeFailed = path.isEmpty();
        QObject::connect(actualTheme(), SIGNAL(themeChanged()), q, SLOT(themeChanged()));
    } else if (QFile::exists(actualPath)) {
        QObject::connect(cacheAndColorsTheme(), SIGNAL(themeChanged()), q, SLOT(themeChanged()), Qt::UniqueConnection);
        path = actualPath;
    } else {
#ifndef NDEBUG
        // qDebug() << "file '" << path << "' does not exist!";
#endif
    }

    // check if svg wants colorscheme applied
    checkColorHints();

    // also images with absolute path needs to have a natural size initialized,
    // even if looks a bit weird using Theme to store non-themed stuff
    if ((themed && QFile::exists(path)) || QFile::exists(actualPath)) {
        QRectF rect;

        if (cacheAndColorsTheme()->findInRectsCache(path, "_Natural", rect)) {
            naturalSize = rect.size();
        } else {
            createRenderer();
            naturalSize = renderer->defaultSize() * scaleFactor;
            //qDebug() << "natural size for" << path << "from renderer is" << naturalSize;
            cacheAndColorsTheme()->insertIntoRectsCache(path, "_Natural", QRectF(QPointF(0, 0), naturalSize));
            //qDebug() << "natural size for" << path << "from cache is" << naturalSize;
        }
    }

    if (!themed) {
        QFile f(actualPath);
        QFileInfo info(f);
        lastModified = info.lastModified().toTime_t();
    }

    emit q->imagePathChanged();

    return updateNeeded;
}

Theme *SvgPrivate::actualTheme()
{
    if (!theme) {
        theme = new Plasma::Theme(q);
    }

    return theme.data();
}

Theme *SvgPrivate::cacheAndColorsTheme()
{
    if (themed) {
        return actualTheme();
    } else {
        // use a separate cache source for unthemed svg's
        if (!s_systemColorsCache) {
            //FIXME: reference count this, so that it is deleted when no longer in use
            s_systemColorsCache = new Plasma::Theme("internal-system-colors");
        }

        return s_systemColorsCache.data();
    }
}

QPixmap SvgPrivate::findInCache(const QString &elementId, const QSizeF &s)
{
    QSize size;
    QString actualElementId;

    if (elementsWithSizeHints.isEmpty()) {
        // Fetch all size hinted element ids from the theme's rect cache
        // and store them locally.
        QRegExp sizeHintedKeyExpr(CACHE_ID_NATURAL_SIZE("(\\d+)-(\\d+)-(.+)", devicePixelRatio));

        foreach (const QString &key, cacheAndColorsTheme()->listCachedRectKeys(path)) {
            if (sizeHintedKeyExpr.exactMatch(key)) {
                QString baseElementId = sizeHintedKeyExpr.cap(3);
                QSize sizeHint(sizeHintedKeyExpr.cap(1).toInt(),
                               sizeHintedKeyExpr.cap(2).toInt());

                if (sizeHint.isValid()) {
                    elementsWithSizeHints.insertMulti(baseElementId, sizeHint);
                }
            }
        }

        if (elementsWithSizeHints.isEmpty()) {
            // Make sure we won't query the theme unnecessarily.
            elementsWithSizeHints.insert(QString(), QSize());
        }
    }

    // Look at the size hinted elements and try to find the smallest one with an
    // identical aspect ratio.
    if (s.isValid() && !elementId.isEmpty()) {
        QList<QSize> elementSizeHints = elementsWithSizeHints.values(elementId);

        if (!elementSizeHints.isEmpty()) {
            QSize bestFit(-1, -1);

            Q_FOREACH (const QSize &hint, elementSizeHints) {

                if (hint.width() >= s.width() && hint.height() >= s.height() &&
                        (!bestFit.isValid() ||
                         (bestFit.width() * bestFit.height()) > (hint.width() * hint.height()))) {
                    bestFit = hint;
                }
            }

            if (bestFit.isValid()) {
                actualElementId = QString::number(bestFit.width()) % "-" %
                                  QString::number(bestFit.height()) % "-" % elementId;
            }
        }
    }

    if (elementId.isEmpty() || !q->hasElement(actualElementId)) {
        actualElementId = elementId;
    }

    if (elementId.isEmpty() || (multipleImages && s.isValid())) {
        size = s.toSize();
    } else {
        size = elementRect(actualElementId).size().toSize();
    }

    if (size.isEmpty()) {
        return QPixmap();
    }

    QString id = cachePath(path, size);

    if (!actualElementId.isEmpty()) {
        id.append(actualElementId);
    }

    //qDebug() << "id is " << id;

    QPixmap p;
    if (cacheRendering && cacheAndColorsTheme()->findInCache(id, p, lastModified)) {
        p.setDevicePixelRatio(q->devicePixelRatio());
        //qDebug() << "found cached version of " << id << p.size();
        return p;
    }

    //qDebug() << "didn't find cached version of " << id << ", so re-rendering";

    //qDebug() << "size for " << actualElementId << " is " << s;
    // we have to re-render this puppy

    createRenderer();

    QRectF finalRect = makeUniform(renderer->boundsOnElement(actualElementId), QRect(QPoint(0, 0), size));

    //don't alter the pixmap size or it won't match up properly to, e.g., FrameSvg elements
    //makeUniform should never change the size so much that it gains or loses a whole pixel
    p = QPixmap(size);

    p.fill(Qt::transparent);
    QPainter renderPainter(&p);

    if (actualElementId.isEmpty()) {
        renderer->render(&renderPainter, finalRect);
    } else {
        renderer->render(&renderPainter, actualElementId, finalRect);
    }

    renderPainter.end();
    p.setDevicePixelRatio(q->devicePixelRatio());

    // Apply current color scheme if the svg asks for it
    if (applyColors) {
        QImage itmp = p.toImage();
        KIconEffect::colorize(itmp, cacheAndColorsTheme()->color(Theme::BackgroundColor), 1.0);
        p = p.fromImage(itmp);
    }

    if (cacheRendering) {
        cacheAndColorsTheme()->insertIntoCache(id, p, QString::number((qint64)q, 16) % QLSEP % actualElementId);
    }

    return p;
}

void SvgPrivate::createRenderer()
{
    if (renderer) {
        return;
    }

    //qDebug() << kBacktrace();
    if (themed && path.isEmpty() && !themeFailed) {
        Applet *applet = qobject_cast<Applet *>(q->parent());
        //FIXME: this maybe could be more efficient if we knew if the package was empty, e.g. for
        //C++; however, I'm not sure this has any real world runtime impact. something to measure
        //for.
        if (applet && applet->kPackage().isValid()) {
            const KPackage::Package package = applet->kPackage();
            path = package.filePath("images", themePath + ".svg");

            if (path.isEmpty()) {
                path = package.filePath("images", themePath + ".svgz");
            }
        }

        if (path.isEmpty()) {
            path = actualTheme()->imagePath(themePath);
            themeFailed = path.isEmpty();
            if (themeFailed) {
                qWarning() << "No image path found for" << themePath;
            }
        }
    }

    //qDebug() << "********************************";
    //qDebug() << "FAIL! **************************";
    //qDebug() << path << "**";

    QString styleSheet = cacheAndColorsTheme()->d->svgStyleSheet(colorGroup);
    styleCrc = qChecksum(styleSheet.toUtf8(), styleSheet.size());

    QHash<QString, SharedSvgRenderer::Ptr>::const_iterator it = s_renderers.constFind(styleCrc + path);

    if (it != s_renderers.constEnd()) {
        //qDebug() << "gots us an existing one!";
        renderer = it.value();
    } else {
        if (path.isEmpty()) {
            renderer = new SharedSvgRenderer();
        } else {
            QHash<QString, QRectF> interestingElements;
            renderer = new SharedSvgRenderer(path, styleSheet, interestingElements);

            // Add interesting elements to the theme's rect cache.
            QHashIterator<QString, QRectF> i(interestingElements);

            while (i.hasNext()) {
                i.next();
                const QString &elementId = i.key();
                const QRectF &elementRect = i.value();

                const QString cacheId = CACHE_ID_NATURAL_SIZE(elementId, devicePixelRatio);
                localRectCache.insert(cacheId, elementRect);
                cacheAndColorsTheme()->insertIntoRectsCache(path, cacheId, elementRect);
            }
        }

        s_renderers[styleCrc + path] = renderer;
    }

    if (size == QSizeF()) {
        size = renderer->defaultSize();
    }
}

void SvgPrivate::eraseRenderer()
{
    if (renderer && renderer->ref.load() == 2) {
        // this and the cache reference it
        s_renderers.erase(s_renderers.find(styleCrc + path));

        if (theme) {
            theme.data()->releaseRectsCache(path);
        }
    }

    renderer = 0;
    styleCrc = 0;
    localRectCache.clear();
    elementsWithSizeHints.clear();
}

QRectF SvgPrivate::elementRect(const QString &elementId)
{
    if (themed && path.isEmpty()) {
        if (themeFailed) {
            return QRectF();
        }

        path = actualTheme()->imagePath(themePath);
        themeFailed = path.isEmpty();

        if (themeFailed) {
            return QRectF();
        }
    }

    if (path.isEmpty()) {
        return QRectF();
    }

    QString id = cacheId(elementId);

    if (localRectCache.contains(id)) {
        return localRectCache.value(id);
    }

    QRectF rect;
    bool found = cacheAndColorsTheme()->findInRectsCache(path, id, rect);

    //This is a corner case where we are *sure* the element is not valid
    if (found && rect == QRectF()) {
        return rect;
    } else if (found) {
        localRectCache.insert(id, rect);
    } else {
        rect = findAndCacheElementRect(elementId);
    }

    return rect;
}

QRectF SvgPrivate::findAndCacheElementRect(const QString &elementId)
{
    createRenderer();

    // createRenderer() can insert some interesting rects in the cache, so check it
    const QString id = cacheId(elementId);
    if (localRectCache.contains(id)) {
        return localRectCache.value(id);
    }

    QRectF elementRect = renderer->elementExists(elementId) ?
                         renderer->matrixForElement(elementId).map(renderer->boundsOnElement(elementId)).boundingRect() :
                         QRectF();
    naturalSize = renderer->defaultSize();
    qreal dx = size.width() / naturalSize.width();
    qreal dy = size.height() / naturalSize.height();

    elementRect = QRectF(elementRect.x() * dx, elementRect.y() * dy,
                         elementRect.width() * dx * scaleFactor, elementRect.height() * dy * scaleFactor);

    cacheAndColorsTheme()->insertIntoRectsCache(path, id, elementRect);

    return elementRect;
}

QMatrix SvgPrivate::matrixForElement(const QString &elementId)
{
    createRenderer();
    return renderer->matrixForElement(elementId);
}

void SvgPrivate::checkColorHints()
{
    if (elementRect("hint-apply-color-scheme").isValid()) {
        applyColors = true;
        usesColors = true;
    } else if (elementRect("current-color-scheme").isValid()) {
        applyColors = false;
        usesColors = true;
    } else {
        applyColors = false;
        usesColors = false;
    }

    // check to see if we are using colors, but the theme isn't being used or isn't providing
    // a colorscheme
    if (qApp) {
        if (usesColors && (!themed || !actualTheme()->colorScheme())) {
            qApp->installEventFilter(q);
        } else {
            qApp->removeEventFilter(q);
        }
    }
}

bool Svg::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance()) {
        if (event->type() == QEvent::ApplicationPaletteChange) {
            d->colorsChanged();
        }
    }
    return QObject::eventFilter(watched, event);
}

//Following two are utility functions to snap rendered elements to the pixel grid
//to and from are always 0 <= val <= 1
qreal SvgPrivate::closestDistance(qreal to, qreal from)
{
    qreal a = to - from;
    if (qFuzzyCompare(to, from)) {
        return 0;
    } else if (to > from) {
        qreal b = to - from - 1;
        return (qAbs(a) > qAbs(b)) ?  b : a;
    } else {
        qreal b = 1 + to - from;
        return (qAbs(a) > qAbs(b)) ? b : a;
    }
}

QRectF SvgPrivate::makeUniform(const QRectF &orig, const QRectF &dst)
{
    if (qFuzzyIsNull(orig.x()) || qFuzzyIsNull(orig.y())) {
        return dst;
    }

    QRectF res(dst);
    qreal div_w = dst.width() / orig.width();
    qreal div_h = dst.height() / orig.height();

    qreal div_x = dst.x() / orig.x();
    qreal div_y = dst.y() / orig.y();

    //horizontal snap
    if (!qFuzzyIsNull(div_x) && !qFuzzyCompare(div_w, div_x)) {
        qreal rem_orig = orig.x() - (floor(orig.x()));
        qreal rem_dst = dst.x() - (floor(dst.x()));
        qreal offset = closestDistance(rem_dst, rem_orig);
        res.translate(offset + offset * div_w, 0);
        res.setWidth(res.width() + offset);
    }
    //vertical snap
    if (!qFuzzyIsNull(div_y) && !qFuzzyCompare(div_h, div_y)) {
        qreal rem_orig = orig.y() - (floor(orig.y()));
        qreal rem_dst = dst.y() - (floor(dst.y()));
        qreal offset = closestDistance(rem_dst, rem_orig);
        res.translate(0, offset + offset * div_h);
        res.setHeight(res.height() + offset);
    }

    //qDebug()<<"Aligning Rects, origin:"<<orig<<"destination:"<<dst<<"result:"<<res;
    return res;
}

void SvgPrivate::themeChanged()
{
    if (q->imagePath().isEmpty()) {
        return;
    }

    if (themed) {
        // check if new theme svg wants colorscheme applied
        checkColorHints();
    }

    QString currentPath = themed ? themePath : path;
    themePath.clear();
    eraseRenderer();
    setImagePath(currentPath);
    q->resize();

    //qDebug() << themePath << ">>>>>>>>>>>>>>>>>> theme changed";
    emit q->repaintNeeded();
}

void SvgPrivate::colorsChanged()
{
    if (!usesColors) {
        return;
    }

    eraseRenderer();
    //qDebug() << "repaint needed from colorsChanged";

    // in the case the theme follows the desktop settings, refetch the colorschemes
    // and discard the svg pixmap cache
    if (!theme.data()->d->colors) {
        KSharedConfig::openConfig()->reparseConfiguration();
        theme.data()->d->colorScheme = KColorScheme(QPalette::Active, KColorScheme::Window);
        theme.data()->d->buttonColorScheme = KColorScheme(QPalette::Active, KColorScheme::Button);
        theme.data()->d->viewColorScheme = KColorScheme(QPalette::Active, KColorScheme::View);
        theme.data()->d->discardCache(PixmapCache | SvgElementsCache);
    }

    emit q->repaintNeeded();
}

QHash<QString, SharedSvgRenderer::Ptr> SvgPrivate::s_renderers;
QWeakPointer<Theme> SvgPrivate::s_systemColorsCache;

Svg::Svg(QObject *parent)
    : QObject(parent),
      d(new SvgPrivate(this))
{
}

Svg::~Svg()
{
    delete d;
}

void Svg::setDevicePixelRatio(qreal ratio)
{
    //be completely integer for now
    //devicepixelratio is always set integer in the svg, so needs at least 192dpi to double up.
    //(it needs to be integer to have lines contained inside a svg piece to keep being pixel aligned)
    if (floor(d->devicePixelRatio) == floor(ratio)) {
        return;
    }

    d->devicePixelRatio = floor(ratio);

    emit repaintNeeded();
}

qreal Svg::devicePixelRatio()
{
    return d->devicePixelRatio;
}


void Svg::setScaleFactor(qreal ratio)
{
    //be completely integer for now
    //devicepixelratio is always set integer in the svg, so needs at least 192dpi to double up.
    //(it needs to be integer to have lines contained inside a svg piece to keep being pixel aligned)
    if (floor(d->scaleFactor) == floor(ratio)) {
        return;
    }

    d->scaleFactor = floor(ratio);

    emit repaintNeeded();
    emit sizeChanged();
}

qreal Svg::scaleFactor() const
{
    return d->scaleFactor;
}

void Svg::setColorGroup(Plasma::Theme::ColorGroup group)
{
    if (d->colorGroup == group) {
        return;
    }

    d->colorGroup = group;
    d->renderer = 0;
    emit colorGroupChanged();
    emit repaintNeeded();
}

Plasma::Theme::ColorGroup Svg::colorGroup() const
{
    return d->colorGroup;
}

QPixmap Svg::pixmap(const QString &elementID)
{
    if (elementID.isNull() || d->multipleImages) {
        return d->findInCache(elementID, size() * d->devicePixelRatio);
    } else {
        return d->findInCache(elementID, d->elementRect(elementID).size() * d->devicePixelRatio);
    }
}

QImage Svg::image(const QSize &size, const QString &elementID)
{
    QPixmap pix(d->findInCache(elementID, size * d->devicePixelRatio));
    return pix.toImage();
}

void Svg::paint(QPainter *painter, const QPointF &point, const QString &elementID)
{
    Q_ASSERT(painter->device());
    const int ratio = painter->device()->devicePixelRatio();
    QPixmap pix((elementID.isNull() || d->multipleImages) ? d->findInCache(elementID, size() * ratio) :
                d->findInCache(elementID, d->elementRect(elementID).size() * ratio));

    if (pix.isNull()) {
        return;
    }

    painter->drawPixmap(QRectF(point, size()), pix, QRectF(QPointF(0, 0), pix.size()));
}

void Svg::paint(QPainter *painter, int x, int y, const QString &elementID)
{
    paint(painter, QPointF(x, y), elementID);
}

void Svg::paint(QPainter *painter, const QRectF &rect, const QString &elementID)
{
    Q_ASSERT(painter->device());
    const int ratio = painter->device()->devicePixelRatio();
    QPixmap pix(d->findInCache(elementID, rect.size() * ratio));

    painter->drawPixmap(QRectF(rect.topLeft(), rect.size()), pix, QRectF(QPointF(0, 0), pix.size()));
}

void Svg::paint(QPainter *painter, int x, int y, int width, int height, const QString &elementID)
{
    Q_ASSERT(painter->device());
    const int ratio = painter->device()->devicePixelRatio();
    QPixmap pix(d->findInCache(elementID, QSizeF(width, height) * ratio));
    painter->drawPixmap(x, y, pix, 0, 0, pix.size().width(), pix.size().height());
}

QSize Svg::size() const
{
    if (d->size.isEmpty()) {
        d->size = d->naturalSize;
    }

    return d->size.toSize();
}

void Svg::resize(qreal width, qreal height)
{
    resize(QSize(width, height));
}

void Svg::resize(const QSizeF &size)
{
    if (qFuzzyCompare(size.width(), d->size.width()) &&
            qFuzzyCompare(size.height(), d->size.height())) {
        return;
    }

    d->size = size;
    d->localRectCache.clear();
    emit sizeChanged();
}

void Svg::resize()
{
    if (qFuzzyCompare(d->naturalSize.width(), d->size.width()) &&
            qFuzzyCompare(d->naturalSize.height(), d->size.height())) {
        return;
    }

    d->size = d->naturalSize;
    d->localRectCache.clear();
    emit sizeChanged();
}

QSize Svg::elementSize(const QString &elementId) const
{
    return d->elementRect(elementId).size().toSize();
}

QRectF Svg::elementRect(const QString &elementId) const
{
    return d->elementRect(elementId);
}

bool Svg::hasElement(const QString &elementId) const
{
    if (d->path.isNull() && d->themePath.isNull()) {
        return false;
    }

    return d->elementRect(elementId).isValid();
}

bool Svg::isValid() const
{
    if (d->path.isNull() && d->themePath.isNull()) {
        return false;
    }

    //try very hard to avoid creation of a parser
    QRectF rect;
    if (d->cacheAndColorsTheme()->findInRectsCache(d->path, "_Natural", rect)) {
        return true;
    }

    if (!QFile::exists(d->path)) {
        return false;
    }

    d->createRenderer();
    return d->renderer->isValid();
}

void Svg::setContainsMultipleImages(bool multiple)
{
    d->multipleImages = multiple;
}

bool Svg::containsMultipleImages() const
{
    return d->multipleImages;
}

void Svg::setImagePath(const QString &svgFilePath)
{
    if (d->setImagePath(svgFilePath)) {
        //qDebug() << "repaintNeeded";
        emit repaintNeeded();
    }
}

QString Svg::imagePath() const
{
    return d->themed ? d->themePath : d->path;
}

void Svg::setUsingRenderingCache(bool useCache)
{
    d->cacheRendering = useCache;
}

bool Svg::isUsingRenderingCache() const
{
    return d->cacheRendering;
}

bool Svg::fromCurrentTheme() const
{
    return d->fromCurrentTheme;
}

void Svg::setTheme(Plasma::Theme *theme)
{
    if (!theme || theme == d->theme.data()) {
        return;
    }

    if (d->theme) {
        disconnect(d->theme.data(), 0, this, 0);
    }

    d->theme = theme;
    connect(theme, SIGNAL(themeChanged()), this, SLOT(themeChanged()));
    d->themeChanged();
}

Theme *Svg::theme() const
{
    return d->actualTheme();
}

} // Plasma namespace

#include "moc_svg.cpp"
