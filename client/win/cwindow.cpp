#include "stdafx.h"

void CWindow::ShowWindow(int nCmdShow)
{
    ::ShowWindow(m_hwnd, nCmdShow);
}

void CWindow::UpdateWindow(void)
{
    ::UpdateWindow(m_hwnd);
}

bool CWindow::DrawMenuBar(void)
{
    return (0 == ::DrawMenuBar(m_hwnd));
}

LRESULT CWindow::SendMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    return ::SendMessage(m_hwnd, message, wParam, lParam);
}

void CWindow::MoveWindow(int x, int y, int cx, int cy, bool bRepaint)
{
    ::MoveWindow(m_hwnd, x, y, cx, cy, bRepaint ? TRUE : FALSE);
}

LRESULT CWindow::DefaultWindowHandler(UINT message, WPARAM wParam, LPARAM lParam)
{
    return ::DefWindowProc(m_hwnd, message, wParam, lParam);
}

LRESULT CWindow::DefaultMDIFrameHandler(UINT message, WPARAM wParam, LPARAM lParam)
{
    return ::DefFrameProc(m_pParentWindow->m_hwnd, m_hwnd, message, wParam, lParam);
}

LRESULT CWindow::DefaultMDIChildHandler(UINT message, WPARAM wParam, LPARAM lParam)
{
    return ::DefMDIChildProc(m_hwnd, message, wParam, lParam);
}

CWindow::CWindow()
{
    m_hwnd = NULL;
    m_pParentWindow = NULL;
}

CWindow::~CWindow()
{
}
