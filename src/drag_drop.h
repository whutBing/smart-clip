#pragma once
#include <windows.h>
#include <oleidl.h>
#include <shlobj.h>

// 拖拽阈值（像素）
#define DRAG_THRESHOLD 5

// 拖放状态变量
extern bool g_isDragging;
extern POINT g_dragStartPoint;
extern int g_dragItemIndex;
extern bool g_dragOccurred;
extern int g_dropTargetIndex;
extern bool g_isDropTargetValid;

// 拖放控制
void StartDragOperation(HWND hwnd, int itemIndex);
void EndDragOperation(HWND hwnd);
void CancelDragOperation(HWND hwnd);

// 拖放处理
void HandleDragMove(HWND hwnd, int mouseX, int mouseY);
bool HandleDragDrop(HWND hwnd, int mouseX, int mouseY);

// CDropSource 类
class CDropSource : public IDropSource {
public:
    CDropSource();
    virtual ~CDropSource();

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IDropSource methods
    STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD grfKeyState);
    STDMETHOD(GiveFeedback)(DWORD dwEffect);

private:
    LONG m_refCount;
};

// CDropTarget 类（含 IDropTargetHelper，用于显示拖拽图像）
class CDropTarget : public IDropTarget {
public:
    CDropTarget();
    virtual ~CDropTarget();

    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv);
    STDMETHOD_(ULONG, AddRef)();
    STDMETHOD_(ULONG, Release)();

    // IDropTarget methods
    STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragLeave)();
    STDMETHOD(Drop)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect);

private:
    LONG m_refCount;
    IDropTargetHelper *m_pDropTargetHelper;
};
