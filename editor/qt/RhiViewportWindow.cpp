#include "RhiViewportWindow.h"

#include <QEvent>
#include <QContextMenuEvent>
#include <QMouseEvent>

namespace Demon {

RhiViewportWindow::RhiViewportWindow()
{
    // Matches Qt's rhiwindow example: Qt owns the native Direct3D surface,
    // while DemonRuntime records the D3D12 swapchain commands for that surface.
    setSurfaceType(QSurface::Direct3DSurface);
    setTitle(QStringLiteral("DemonEngine Viewport"));
    setFlags(Qt::FramelessWindowHint);
}

bool RhiViewportWindow::event(QEvent* event)
{
    if (event->type() == QEvent::UpdateRequest)
        return true;
    if (event->type() == QEvent::ContextMenu) {
        auto* contextEvent = static_cast<QContextMenuEvent*>(event);
        if (!m_rightDragged && m_contextMenuCallback) {
            m_contextMenuCallback(contextEvent->globalPos());
            contextEvent->accept();
            return true;
        }
    }
    return QWindow::event(event);
}

void RhiViewportWindow::mousePressEvent(QMouseEvent* event)
{
    requestActivate();
    if (event->button() == Qt::LeftButton && m_pickCallback && width() > 0 && height() > 0) {
        m_leftDown = true;
        const float ndcX = (2.0f * static_cast<float>(event->position().x()) / static_cast<float>(width())) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(event->position().y()) / static_cast<float>(height()));
        m_pickCallback(ndcX, ndcY);
    } else if (event->button() == Qt::RightButton) {
        m_rightPressPosition = event->position().toPoint();
        m_rightDragged = false;
    }
    QWindow::mousePressEvent(event);
}

void RhiViewportWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_leftDown && m_pointerMoveCallback && width() > 0 && height() > 0) {
        const float ndcX = (2.0f * static_cast<float>(event->position().x()) / static_cast<float>(width())) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(event->position().y()) / static_cast<float>(height()));
        m_pointerMoveCallback(ndcX, ndcY);
    }
    if (event->buttons().testFlag(Qt::RightButton) &&
        (event->position().toPoint() - m_rightPressPosition).manhattanLength() > 4)
        m_rightDragged = true;
    QWindow::mouseMoveEvent(event);
}

void RhiViewportWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_leftDown = false;
        if (m_pointerReleaseCallback && width() > 0 && height() > 0) {
            const float ndcX = (2.0f * static_cast<float>(event->position().x()) / static_cast<float>(width())) - 1.0f;
            const float ndcY = 1.0f - (2.0f * static_cast<float>(event->position().y()) / static_cast<float>(height()));
            m_pointerReleaseCallback(ndcX, ndcY);
        }
    }
    QWindow::mouseReleaseEvent(event);
}

} // namespace Demon
