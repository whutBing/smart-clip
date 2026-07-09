#include "drag_drop.h"
#include "smartclip.h"
#include "history.h"
#include <shlobj.h>
#include <commctrl.h>
#include <windowsx.h>

// 拖放状态变量定义
bool g_isDragging = false;
POINT g_dragStartPoint = {0, 0};
int g_dragItemIndex = -1;
bool g_dragOccurred = false;
int g_dropTargetIndex = -1;
bool g_isDropTargetValid = false;

// CDropSource 实现
CDropSource::CDropSource() : m_refCount(1) {}

CDropSource::~CDropSource() {}

STDMETHODIMP CDropSource::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDropSource) {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDropSource::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) CDropSource::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

STDMETHODIMP CDropSource::QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) {
    if (fEscapePressed) {
        return DRAGDROP_S_CANCEL;
    }
    if (!(grfKeyState & MK_LBUTTON)) {
        return DRAGDROP_S_DROP;
    }
    return S_OK;
}

STDMETHODIMP CDropSource::GiveFeedback(DWORD dwEffect) {
    (void)dwEffect;
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

// CDropTarget 实现（含 IDropTargetHelper，用于显示拖拽图像）
CDropTarget::CDropTarget() : m_refCount(1), m_pDropTargetHelper(NULL) {
    CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER,
                     IID_IDropTargetHelper, (void **)&m_pDropTargetHelper);
}

CDropTarget::~CDropTarget() {
    if (m_pDropTargetHelper)
        m_pDropTargetHelper->Release();
}

STDMETHODIMP CDropTarget::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppv = static_cast<IDropTarget *>(this);
        AddRef();
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CDropTarget::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) CDropTarget::Release() {
    ULONG count = InterlockedDecrement(&m_refCount);
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

STDMETHODIMP CDropTarget::DragEnter(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    *pdwEffect = DROPEFFECT_COPY;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->DragEnter(g_hwndMain, pDataObj, &point, *pdwEffect);
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    *pdwEffect = DROPEFFECT_COPY;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->DragOver(&point, *pdwEffect);
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::DragLeave() {
    if (m_pDropTargetHelper) {
        m_pDropTargetHelper->DragLeave();
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->Drop(pDataObj, &point, *pdwEffect);
    }
    *pdwEffect = DROPEFFECT_NONE;
    return S_OK;
}

// 拖放控制函数
void StartDragOperation(HWND hwnd, int itemIndex) {
    if (!hwnd || itemIndex < 0)
        return;

    g_isDragging = true;
    g_dragItemIndex = itemIndex;
    GetCursorPos(&g_dragStartPoint);
}

void EndDragOperation(HWND hwnd) {
    (void)hwnd;
    g_isDragging = false;
    g_dragItemIndex = -1;
    g_dragOccurred = false;
    g_dropTargetIndex = -1;
    g_isDropTargetValid = false;
}

void CancelDragOperation(HWND hwnd) {
    g_isDragging = false;
    g_dragItemIndex = -1;
    g_dragOccurred = false;
    g_dropTargetIndex = -1;
    g_isDropTargetValid = false;
    InvalidateRect(hwnd, NULL, TRUE);
}

void HandleDragMove(HWND hwnd, int mouseX, int mouseY) {
    if (!g_isDragging || !hwnd)
        return;

    POINT pt = {mouseX, mouseY};
    LPARAM result = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    int itemIndex = (int)(short)LOWORD(result);
    
    if (itemIndex >= 0 && itemIndex != g_dragItemIndex) {
        g_dropTargetIndex = itemIndex;
        g_isDropTargetValid = true;
        g_dragOccurred = true;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

bool HandleDragDrop(HWND hwnd, int mouseX, int mouseY) {
    if (!g_isDragging || !hwnd)
        return false;

    POINT pt = {mouseX, mouseY};
    LPARAM result = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, MAKELPARAM(pt.x, pt.y));
    int targetIndex = (int)(short)LOWORD(result);
    
    if (targetIndex >= 0 && targetIndex != g_dragItemIndex && g_isDropTargetValid) {
        // 执行拖放操作：交换项目（g_dragItemIndex/targetIndex 即 displayIndex）
        if (g_dragItemIndex >= 0 && g_dragItemIndex < (int)g_displayIndexMap.size() &&
            targetIndex >= 0 && targetIndex < (int)g_displayIndexMap.size()) {
            // 交换历史记录顺序
            SwapHistoryItems(g_dragItemIndex, targetIndex);

            // 刷新列表
            RefreshListBox();
            EndDragOperation(hwnd);
            return true;
        }
    }
    
    CancelDragOperation(hwnd);
    return false;
}
