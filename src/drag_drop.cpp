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

// 定义在 smartclip.cpp：主窗体当前是否由"大幅拖拽呼出"（文件中转站）
extern bool g_dragShelfSummoned;

// 数据对象是否携带 CF_HDROP（文件/文件夹）
static bool HasFilePayload(IDataObject *pDataObj) {
    if (!pDataObj)
        return false;
    FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return pDataObj->QueryGetData(&fe) == S_OK;
}

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
CDropTarget::CDropTarget() : m_refCount(1), m_pDropTargetHelper(NULL), m_hasFile(false) {
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
    // 仅接受文件/文件夹（CF_HDROP），其他数据给"禁止"光标
    m_hasFile = HasFilePayload(pDataObj);
    *pdwEffect = m_hasFile ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->DragEnter(g_hwndMain, pDataObj, &point, *pdwEffect);
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::DragOver(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    *pdwEffect = m_hasFile ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->DragOver(&point, *pdwEffect);
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::DragLeave() {
    m_hasFile = false;
    if (m_pDropTargetHelper) {
        m_pDropTargetHelper->DragLeave();
    }
    return S_OK;
}

STDMETHODIMP CDropTarget::Drop(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) {
    (void)grfKeyState;
    if (m_pDropTargetHelper) {
        POINT point = {pt.x, pt.y};
        m_pDropTargetHelper->Drop(pDataObj, &point, m_hasFile ? DROPEFFECT_COPY : DROPEFFECT_NONE);
    }
    *pdwEffect = DROPEFFECT_NONE;
    m_hasFile = false;

    const bool wasSummoned = g_dragShelfSummoned;
    std::wstring joined;
    FORMATETC fe = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg = {};
    if (pDataObj && SUCCEEDED(pDataObj->GetData(&fe, &stg)) && stg.hGlobal) {
        HDROP hDrop = (HDROP)GlobalLock(stg.hGlobal);
        if (hDrop) {
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            wchar_t buf[MAX_PATH];
            for (UINT i = 0; i < count; i++) {
                if (DragQueryFileW(hDrop, i, buf, MAX_PATH) > 0) {
                    if (!joined.empty())
                        joined += L'\n';
                    joined += buf;
                }
            }
            GlobalUnlock(stg.hGlobal);
        }
        ReleaseStgMedium(&stg);
    }

    if (!joined.empty()) {
        // 文件/文件夹落地：存入历史（文件中转站）
        AddFilesToHistory(joined, nullptr);
        *pdwEffect = DROPEFFECT_COPY;
        // wParam: 1=拖拽呼出后落地（切到文件页展示），0=窗体本就可见
        if (g_hwndMain && IsWindow(g_hwndMain))
            PostMessageW(g_hwndMain, WM_USER + 0x2000, wasSummoned ? 1 : 0, 0);
    } else if (wasSummoned) {
        // 数据未被接受：隐藏呼出的窗体并还原位置
        if (g_hwndMain && IsWindow(g_hwndMain))
            PostMessageW(g_hwndMain, WM_USER + 0x2000, 2, 0);
    }
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
