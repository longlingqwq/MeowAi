#pragma once

class QObject;
class QWindow;

namespace MeowAiPlatform {

/**
 * 为**无边框窗口**安装原生缩放边框助手（阶段三）。
 *
 * Windows：接管 WM_NCHITTEST（边框 → HTLEFT/HTTOP 等缩放命中码）、
 * WM_NCCALCSIZE（去掉系统绘制的非客户区）、WM_GETMINMAXINFO（最大化时贴合工作区，
 * 不覆盖任务栏），并补上 WS_THICKFRAME 以确保命中码真正触发缩放。
 *
 * 其它平台：返回 nullptr —— 由 QML 侧 startSystemResize 兜底（见 ui/main.qml）。
 *
 * 返回对象由 parent 托管，调用方无需释放。
 */
QObject *installTitleBarHelper(QWindow *window, QObject *parent = nullptr);

} // namespace MeowAiPlatform
