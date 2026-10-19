#include "platform/TitleBarHelper.h"

// 必须先引入 QtGlobal：Q_OS_WIN 来自 qsystemdetection.h，是编译器宏无法提供的
#include <QtGlobal>

#if defined(Q_OS_WIN)

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QWindow>

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

namespace {

/// 可缩放边框宽度（物理像素）
constexpr int kResizeBorder = 6;

/**
 * Windows 无边框窗口原生助手。
 *
 * 只做 QML 做不到的事：在**命中测试**阶段返回缩放边框码，并保证窗口具备
 * WS_THICKFRAME，从而让系统真正进入缩放循环。拖拽仍由 QML 的
 * startSystemMove() 触发，因此**不会**出现“无法拖动 / 无法关闭”。
 */
class WindowsTitleBarHelper : public QObject, public QAbstractNativeEventFilter
{
public:
    WindowsTitleBarHelper(QWindow *window, QObject *parent)
        : QObject(parent)
        , m_window(window)
    {
        if (!m_window) {
            return;
        }
        m_hwnd = reinterpret_cast<HWND>(m_window->winId());
        if (!m_hwnd) {
            return;
        }
        applyFramelessStyles();
        if (QCoreApplication *app = QCoreApplication::instance()) {
            app->installNativeEventFilter(this);
        }
    }

    ~WindowsTitleBarHelper() override
    {
        if (QCoreApplication *app = QCoreApplication::instance()) {
            app->removeNativeEventFilter(this);
        }
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        if (eventType != QByteArrayLiteral("windows_generic_MSG") || !m_hwnd || !result) {
            return false;
        }
        auto *msg = static_cast<MSG *>(message);
        if (!msg || msg->hwnd != m_hwnd) {
            return false;
        }

        switch (msg->message) {
        case WM_NCCALCSIZE:
            // 自绘标题栏：客户区即整窗，系统不再绘制边框与标题栏
            if (msg->wParam) {
                *result = 0;
                return true;
            }
            break;
        case WM_GETMINMAXINFO: {
            // 最大化时按显示器工作区收缩，避免盖住任务栏
            auto *info = reinterpret_cast<MINMAXINFO *>(msg->lParam);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (info && GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST),
                                        &monitor)) {
                info->ptMaxPosition.x = monitor.rcWork.left - monitor.rcMonitor.left;
                info->ptMaxPosition.y = monitor.rcWork.top - monitor.rcMonitor.top;
                info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
                info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
                *result = 0;
                return true;
            }
            break;
        }
        case WM_NCHITTEST:
            return handleHitTest(msg, result);
        default:
            break;
        }
        return false;
    }

private:
    /// 让无边框窗口具备系统缩放能力，同时关闭 DWM 绘制的非客户区
    void applyFramelessStyles()
    {
        LONG_PTR style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        style &= ~WS_POPUP;
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);

        const DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
        ::DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /// 命中测试：仅在外圈边框返回缩放码，其余交回 Qt/QML（按钮点击、startSystemMove 拖拽）
    bool handleHitTest(MSG *msg, qintptr *result)
    {
        if (IsZoomed(m_hwnd)) {
            return false; // 最大化窗口不做缩放命中，避免误触
        }

        POINT point{ GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
        if (!ScreenToClient(m_hwnd, &point)) {
            return false;
        }
        RECT client{};
        if (!GetClientRect(m_hwnd, &client)) {
            return false;
        }
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        const bool left = point.x < kResizeBorder;
        const bool right = point.x >= width - kResizeBorder;
        const bool top = point.y < kResizeBorder;
        const bool bottom = point.y >= height - kResizeBorder;

        if (top && left) {
            *result = HTTOPLEFT;
        } else if (top && right) {
            *result = HTTOPRIGHT;
        } else if (bottom && left) {
            *result = HTBOTTOMLEFT;
        } else if (bottom && right) {
            *result = HTBOTTOMRIGHT;
        } else if (left) {
            *result = HTLEFT;
        } else if (right) {
            *result = HTRIGHT;
        } else if (top) {
            *result = HTTOP;
        } else if (bottom) {
            *result = HTBOTTOM;
        } else {
            return false;
        }
        return true;
    }

    QWindow *m_window = nullptr;
    HWND m_hwnd = nullptr;
};

} // namespace

#endif // Q_OS_WIN

QObject *MeowAiPlatform::installTitleBarHelper(QWindow *window, QObject *parent)
{
#if defined(Q_OS_WIN)
    return new WindowsTitleBarHelper(window, parent);
#else
    Q_UNUSED(window);
    Q_UNUSED(parent);
    return nullptr; // QML 侧 startSystemResize 兜底
#endif
}
