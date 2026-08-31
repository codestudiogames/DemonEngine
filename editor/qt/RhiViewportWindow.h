#pragma once

#include <QWindow>
#include <QPoint>
#include <functional>

class QMouseEvent;

namespace Demon {

class RhiViewportWindow final : public QWindow {
public:
    RhiViewportWindow();

    void setPickCallback(std::function<void(float, float)> callback) { m_pickCallback = std::move(callback); }
    void setPointerMoveCallback(std::function<void(float, float)> callback) { m_pointerMoveCallback = std::move(callback); }
    void setPointerReleaseCallback(std::function<void(float, float)> callback) { m_pointerReleaseCallback = std::move(callback); }
    void setContextMenuCallback(std::function<void(const QPoint&)> callback) { m_contextMenuCallback = std::move(callback); }

protected:
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::function<void(float, float)> m_pickCallback;
    std::function<void(float, float)> m_pointerMoveCallback;
    std::function<void(float, float)> m_pointerReleaseCallback;
    std::function<void(const QPoint&)> m_contextMenuCallback;
    QPoint m_rightPressPosition;
    bool m_rightDragged = false;
    bool m_leftDown = false;
};

} // namespace Demon
