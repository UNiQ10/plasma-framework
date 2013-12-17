/***************************************************************************
 *   Copyright 2011 Marco Martin <mart@kde.org>                            *
 *   Copyright 2013 Sebastian Kügler <sebas@kde.org>                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "dialog.h"
#include "framesvgitem.h"
#include "dialogshadows_p.h"

#include <QApplication>
#include <QQuickItem>
#include <QDesktopWidget>
#include <QQuickItem>
#include <QQuickItem>
#include <QTimer>
#include <QLayout>
#include <QScreen>

#include <kwindowsystem.h>
#include <kwindoweffects.h>
#include <Plasma/Plasma>
#include <Plasma/Corona>

#include <QDebug>

DialogProxy::DialogProxy(QQuickItem *parent)
    : QQuickWindow(parent ? parent->window() : 0),
      m_location(Plasma::Types::BottomEdge),
      m_activeWindow(false),
      m_type(Normal),
      m_hideOnWindowDeactivate(false)
{
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    setFormat(format);
    setClearBeforeRendering(true);
    setColor(QColor(Qt::transparent));
    setFlags(Qt::FramelessWindowHint);

    m_flags = flags();

    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(250);
    connect(m_syncTimer, &QTimer::timeout, this,  &DialogProxy::syncToMainItemSize);

    //Can't just connect to start() since it can't resolve the overload
    connect(this, &QWindow::xChanged, [=](){m_syncTimer->start();});
    connect(this, &QWindow::yChanged, [=](){m_syncTimer->start();});
    connect(this, &QWindow::visibleChanged, this, &DialogProxy::onVisibleChanged);
    //HACK: this property is invoked due to the initialization that gets done to contentItem() in the getter
    property("data");
    //Create the FrameSvg background.
    m_frameSvgItem = new Plasma::FrameSvgItem(contentItem());
    m_frameSvgItem->setImagePath("dialogs/background");
    //m_frameSvgItem->setImagePath("widgets/background"); // larger borders, for testing those
}

DialogProxy::~DialogProxy()
{
    DialogShadows::self()->removeWindow(this);
}

QQuickItem *DialogProxy::mainItem() const
{
    return m_mainItem.data();
}

void DialogProxy::setMainItem(QQuickItem *mainItem)
{
    if (m_mainItem.data() != mainItem) {
        if (m_mainItem) {
            m_mainItem.data()->setParent(parent());
        }

        m_mainItem = mainItem;

        if (mainItem) {
            //mainItem->setParentItem(0);
            mainItem->setParent(contentItem());
            mainItem->setProperty("parent", QVariant::fromValue(contentItem()));

            if (mainItem->metaObject()->indexOfSignal("widthChanged")) {
                connect(mainItem, SIGNAL(widthChanged()), m_syncTimer, SLOT(start()));
            }
            if (mainItem->metaObject()->indexOfSignal("heightChanged")) {
                connect(mainItem, SIGNAL(heightChanged()), m_syncTimer, SLOT(start()));
            }
            syncToMainItemSize();
        }

        //if this is called in Compenent.onCompleted we have to wait a loop the item is added to a scene
        emit mainItemChanged();
    }
}

QQuickItem *DialogProxy::visualParent() const
{
    return m_visualParent.data();
}

void DialogProxy::setVisualParent(QQuickItem *visualParent)
{
    if (m_visualParent.data() == visualParent) {
        return;
    }

    m_visualParent = visualParent;
    emit visualParentChanged();
    if (visualParent && isVisible()) {
        setPosition(popupPosition(visualParent, Qt::AlignCenter));
    }
}

void DialogProxy::onVisibleChanged()
{
    const bool visible = isVisible();
    if (visible) {
        if (location() == Plasma::Types::FullScreen) {
            m_frameSvgItem->setEnabledBorders(Plasma::FrameSvg::NoBorder);
            setPosition(screen()->availableGeometry().topLeft());
            setGeometry(screen()->availableGeometry());

        } else {
            // FIXME: abuse of flags
            // The dialog hiding is animated, keep it raised while it's animating
            // This seems to be a bit of a hack, but as it works, let's use it for
            // now and keep looking for a better solution.
            setFlags(Qt::WindowStaysOnTopHint|m_flags);
            //syncToMainItemSize();
            setPosition(popupPosition(m_visualParent.data(), Qt::AlignCenter));
            syncToMainItemSize();
        }
    }

    KWindowEffects::SlideFromLocation slideLocation = KWindowEffects::NoEdge;

    switch (m_location) {
    case Plasma::Types::TopEdge:
        slideLocation = KWindowEffects::TopEdge;
        break;
    case Plasma::Types::LeftEdge:
        slideLocation = KWindowEffects::LeftEdge;
        break;
    case Plasma::Types::RightEdge:
        slideLocation = KWindowEffects::RightEdge;
        break;
    case Plasma::Types::BottomEdge:
        slideLocation = KWindowEffects::BottomEdge;
        break;
    //no edge, no slide
    default:
        break;
    }

    KWindowEffects::slideWindow(winId(), slideLocation, -1);

    if (visible) {
        raise();
        KWindowSystem::setState(winId(), NET::SkipTaskbar | NET::SkipPager);

        KWindowSystem::setType(winId(), (NET::WindowType)m_type);
        if (m_type == Dock) {
            KWindowSystem::setOnAllDesktops(winId(), true);
        } else {
            KWindowSystem::setOnAllDesktops(winId(), false);
        }
        setFlags(Qt::FramelessWindowHint|m_flags);
    }
}

QPoint DialogProxy::popupPosition(QQuickItem *item, Qt::AlignmentFlag alignment)
{
    if (!item) {
        //If no item was specified try to align at the center of the parent view
        QQuickItem *parentItem = qobject_cast<QQuickItem *>(parent());
        if (parentItem && parentItem->window()) {
            switch (m_location) {
            case Plasma::Types::TopEdge:
                return QPoint(screen()->availableGeometry().center().x() - width()/2, screen()->availableGeometry().y());
                break;
            case Plasma::Types::LeftEdge:
                return QPoint(screen()->availableGeometry().x(), screen()->availableGeometry().center().y() - height()/2);
                break;
            case Plasma::Types::RightEdge:
                return QPoint(screen()->availableGeometry().right() - width(), screen()->availableGeometry().center().y() - height()/2);
                break;
            case Plasma::Types::BottomEdge:
                return QPoint(screen()->availableGeometry().center().x() - width()/2, screen()->availableGeometry().bottom()-height());
                break;
            //Default center in the screen
            default:
                return screen()->geometry().center() - QPoint(width()/2, height()/2);
            }
        } else {
            return QPoint();
        }
    }
    QPointF pos = item->mapToScene(QPointF(0, 0));

    if (item->window() && item->window()->screen()) {
        pos = item->window()->mapToGlobal(pos.toPoint());
    } else {
        return QPoint();
    }

    //swap direction if necessary
    if (QApplication::isRightToLeft() && alignment != Qt::AlignCenter) {
        if (alignment == Qt::AlignRight) {
            alignment = Qt::AlignLeft;
        } else {
            alignment = Qt::AlignRight;
        }
    }

    Plasma::Types::Location l = location();

    QPoint topPoint((item->boundingRect().width() - width())/2,
                    -height());
    QPoint bottomPoint((item->boundingRect().width() - width())/2,
                       item->boundingRect().height());
    QPoint leftPoint(-width(),
                     (item->boundingRect().height() - height())/2);

    QPoint rightPoint(item->boundingRect().width(),
                      (item->boundingRect().height() - height())/2);

    QPoint offset(0, 0);
    if (l == Plasma::Types::BottomEdge) {
        offset = bottomPoint;
    } else if (l == Plasma::Types::LeftEdge) {
        offset = leftPoint;
    } else if (l == Plasma::Types::RightEdge) {
        offset = rightPoint;
    } else { // Types::TopEdge
        offset = topPoint;
    }

    const QRect avail = item->window()->screen()->availableGeometry();
    QPoint menuPos = pos.toPoint() + offset;

    const int leftMargin = m_frameSvgItem->margins()->left();
    const int rightMargin = m_frameSvgItem->margins()->right();
    const int topMargin = m_frameSvgItem->margins()->top();
    const int bottomMargin = m_frameSvgItem->margins()->bottom();

    if (menuPos.x() < leftMargin) {
        // popup hits lhs
        if (l == Plasma::Types::TopEdge || l == Plasma::Types::BottomEdge) {
            // move it
            menuPos.setX(0-leftMargin);
        } else {
            // swap edge
            menuPos.setX(pos.x() + rightPoint.x());
        }
    }
    if (menuPos.x() + width() > avail.width() - rightMargin) {
        // popup hits rhs
        if (l == Plasma::Types::TopEdge || l == Plasma::Types::BottomEdge) {
            menuPos.setX(avail.width() - item->boundingRect().width() + rightMargin);
        } else {
            menuPos.setX(pos.x() + leftPoint.x());
        }
    }
    if (menuPos.y() < topMargin) {
        // hitting top
        if (l == Plasma::Types::LeftEdge || l == Plasma::Types::RightEdge) {
            menuPos.setY(0);
        } else {
            menuPos.setY(pos.y() + bottomPoint.y());
        }
    }
    if (menuPos.y() + height() > avail.height() - bottomMargin) {
        // hitting bottom
        if (l == Plasma::Types::TopEdge || l == Plasma::Types::BottomEdge) {
            menuPos.setY(pos.y() + topPoint.y());
        } else {
            menuPos.setY(avail.height() - item->boundingRect().height() + bottomMargin);
        }
    }

    //qDebug() << "Popup position" << menuPos << " Location: Plasma::" <<locString(l);
    return menuPos;
}

bool DialogProxy::isActiveWindow() const
{
    return m_activeWindow;
}

void DialogProxy::activateWindow()
{
    //qDebug();
    setWindowState(Qt::WindowActive);
}

int DialogProxy::windowFlags() const
{
    return (int)m_flags;
}

void DialogProxy::setWindowFlags(const int flags)
{
    m_flags = (Qt::WindowFlags)flags;
    setFlags(Qt::FramelessWindowHint|m_flags);
}

Plasma::Types::Location DialogProxy::location() const
{
    return m_location;
}

void DialogProxy::setLocation(Plasma::Types::Location location)
{
    if (m_location == location) {
        return;
    }
    m_location = location;
    emit locationChanged();
}


QObject *DialogProxy::margins() const
{
    return m_frameSvgItem->margins();
}

void DialogProxy::resizeEvent(QResizeEvent *re)
{
    syncMainItemToSize();
    QQuickWindow::resizeEvent(re);
}

void DialogProxy::syncMainItemToSize()
{
    m_frameSvgItem->setX(0);
    m_frameSvgItem->setY(0);
    m_frameSvgItem->setWidth(width());
    m_frameSvgItem->setHeight(height());
    KWindowEffects::enableBlurBehind(winId(), true, m_frameSvgItem->frameSvg()->mask());

    if (m_mainItem) {
        m_mainItem.data()->setX(m_frameSvgItem->margins()->left());
        m_mainItem.data()->setY(m_frameSvgItem->margins()->top());
        m_mainItem.data()->setWidth(width() - m_frameSvgItem->margins()->left() - m_frameSvgItem->margins()->right());
        m_mainItem.data()->setHeight(height() - m_frameSvgItem->margins()->top() - m_frameSvgItem->margins()->bottom());
    }
}

void DialogProxy::syncToMainItemSize()
{
    if (!m_mainItem) {
        return;
    }
    syncBorders();
    //qDebug() << " main item: " << m_mainItem.
    //FIXME: workaround to prevent dialogs of Popup type disappearing on the second show
    const QSize s = QSize(m_mainItem.data()->width(), m_mainItem.data()->height()) +
                    QSize(m_frameSvgItem->margins()->left() + m_frameSvgItem->margins()->right(),
                          m_frameSvgItem->margins()->top() + m_frameSvgItem->margins()->bottom());
    //resize(0,0);
    qDebug() << "DIALOG mainItem: " << m_mainItem.data()->objectName()<< QSize(m_mainItem.data()->width(), m_mainItem.data()->height());
    qDebug() << "DIALOG resize: " << s;
    resize(s);
    emit widthChanged(s.width());
    emit heightChanged(s.height());
}



void DialogProxy::setType(WindowType type)
{
    if (type == m_type) {
        return;
    }

    m_type = type;
    KWindowSystem::setType(winId(), (NET::WindowType)type);

    if (type == Dock) {
        KWindowSystem::setOnAllDesktops(winId(), true);
    } else {
        KWindowSystem::setOnAllDesktops(winId(), false);
    }

    emit typeChanged();
}

DialogProxy::WindowType DialogProxy::type() const
{
    return m_type;
}

void DialogProxy::focusInEvent(QFocusEvent *ev)
{
    m_activeWindow = true;
    emit activeWindowChanged();
    QQuickWindow::focusInEvent(ev);
}

void DialogProxy::focusOutEvent(QFocusEvent *ev)
{
    m_activeWindow = false;
    emit activeWindowChanged();
    if (m_hideOnWindowDeactivate) {
        setVisible(false);
    }
    QQuickWindow::focusOutEvent(ev);
}

void DialogProxy::showEvent(QShowEvent *event)
{
    DialogShadows::self()->addWindow(this, m_frameSvgItem->enabledBorders());
    QQuickWindow::showEvent(event);
}

void DialogProxy::syncBorders()
{
    //syncToMainItemSize();

    const QRect avail = screen()->availableGeometry();

    m_frameSvgItem->setEnabledBorders(Plasma::FrameSvg::AllBorders);

    int borders = Plasma::FrameSvg::AllBorders;
    if (x() <= avail.x()) {
        borders = borders & ~Plasma::FrameSvg::LeftBorder;
    }
    if (y() <= avail.y()) {
        borders = borders & ~Plasma::FrameSvg::TopBorder;
    }
    if (avail.right() <= x() + width()) {
        borders = borders & ~Plasma::FrameSvg::RightBorder;
    }
    if (avail.bottom() <= y() + height()) {
        borders = borders & ~Plasma::FrameSvg::BottomBorder;
    }
    m_frameSvgItem->setEnabledBorders((Plasma::FrameSvg::EnabledBorder)borders);
    if (isVisible()) {
        DialogShadows::self()->addWindow(this, m_frameSvgItem->enabledBorders());
    }
    //syncToMainItemSize();
}

bool DialogProxy::hideOnWindowDeactivate() const
{
    return m_hideOnWindowDeactivate;
}

void DialogProxy::setHideOnWindowDeactivate(bool hide)
{
    if (flags() & Qt::X11BypassWindowManagerHint) {
        // doesn't get keyboard focus, so let's just ignore it
        return;
    }
    if (m_hideOnWindowDeactivate == hide) {
        return;
    }
    m_hideOnWindowDeactivate = hide;
    emit hideOnWindowDeactivateChanged();
}

#include "dialog.moc"

