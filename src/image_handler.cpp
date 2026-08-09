#include "image_handler.h"
#include <windows.h>
#include <objidl.h>   // MinGW 下必须在 gdiplus.h 之前,提供 PROPID
#include <gdiplus.h>
#include <shlwapi.h>
#include <vector>
#include "i18n.h"

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

using namespace Gdiplus;

// 判断是否为图像文件
bool IsImageFile(const wchar_t* filePath) {
    const wchar_t* ext = PathFindExtensionW(filePath);
    if (ext == NULL || *ext == L'\0') {
        return false;
    }

    return (_wcsicmp(ext, L".png") == 0 ||
            _wcsicmp(ext, L".jpg") == 0 ||
            _wcsicmp(ext, L".jpeg") == 0 ||
            _wcsicmp(ext, L".bmp") == 0 ||
            _wcsicmp(ext, L".gif") == 0 ||
            _wcsicmp(ext, L".ico") == 0);
}

// 加载图像文件
bool LoadImageFile(const wchar_t* filePath, std::vector<BYTE>& imageData, int& width, int& height) {
    // 使用GDI+加载图像
    Bitmap* bitmap = new Bitmap(filePath);
    if (bitmap == NULL || bitmap->GetLastStatus() != Ok) {
        if (bitmap) delete bitmap;
        return false;
    }

    // 获取图像尺寸
    width = bitmap->GetWidth();
    height = bitmap->GetHeight();

    // 验证图像尺寸
    if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
        delete bitmap;
        return false;
    }

    // 创建24位DIB
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 负值表示自顶向下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    // 创建DIB section
    HDC hdc = GetDC(NULL);
    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

    if (hBitmap == NULL || pBits == NULL) {
        ReleaseDC(NULL, hdc);
        delete bitmap;
        return false;
    }

    // 创建Graphics对象并绘制图像
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    Graphics graphics(hdcMem);
    graphics.DrawImage(bitmap, 0, 0, width, height);

    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

    // 复制DIB数据
    DWORD imageSize = ((width * 24 + 31) / 32) * 4 * height;
    imageData.resize(imageSize);
    memcpy(&imageData[0], pBits, imageSize);

    DeleteObject(hBitmap);
    delete bitmap;

    return true;
}

// 图像预览窗口过程
LRESULT CALLBACK ImagePreviewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HBITMAP hBitmap = NULL;
    static int imgWidth = 0;
    static int imgHeight = 0;

    switch (message) {
        case WM_CREATE: {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            ClipboardItem* pItem = (ClipboardItem*)pCreate->lpCreateParams;
            if (pItem && pItem->type == TYPE_IMAGE) {
                // 尝试从原图文件加载
                std::vector<BYTE> originalData;
                int origWidth = 0, origHeight = 0;
                bool loadedFromFile = false;

                if (!pItem->imageFileName.empty()) {
                    loadedFromFile = LoadOriginalImage(pItem->imageFileName, originalData, origWidth, origHeight);
                }

                if (loadedFromFile) {
                    // 使用原图数据
                    imgWidth = origWidth;
                    imgHeight = origHeight;

                    // 创建位图
                    HDC hdc = GetDC(hwnd);
                    hBitmap = CreateCompatibleBitmap(hdc, imgWidth, imgHeight);
                    HDC hdcMem = CreateCompatibleDC(hdc);
                    SelectObject(hdcMem, hBitmap);

                    // 创建BITMAPINFO
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = imgWidth;
                    bmi.bmiHeader.biHeight = -imgHeight;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 24;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    // 绘制图像
                    SetDIBitsToDevice(hdcMem, 0, 0, imgWidth, imgHeight, 0, 0, 0, imgHeight,
                                     &originalData[0], &bmi, DIB_RGB_COLORS);

                    DeleteDC(hdcMem);
                    ReleaseDC(hwnd, hdc);
                } else {
                    // 回退到使用缩略图数据（兼容旧数据）
                    // 懒加载：启动时 imageData 为空，需先从文件加载
                    if (pItem->imageData.empty()) {
                        EnsureItemImageLoaded(*pItem);
                    }
                    if (pItem->imageData.empty()) {
                        // 加载失败，无法预览
                        return 0;
                    }
                    imgWidth = pItem->thumbWidth > 0 ? pItem->thumbWidth : pItem->imageWidth;
                    imgHeight = pItem->thumbHeight > 0 ? pItem->thumbHeight : pItem->imageHeight;

                    // 创建位图
                    HDC hdc = GetDC(hwnd);
                    hBitmap = CreateCompatibleBitmap(hdc, imgWidth, imgHeight);
                    HDC hdcMem = CreateCompatibleDC(hdc);
                    SelectObject(hdcMem, hBitmap);

                    // 创建BITMAPINFO
                    BITMAPINFO bmi = {};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = imgWidth;
                    bmi.bmiHeader.biHeight = -imgHeight;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 24;
                    bmi.bmiHeader.biCompression = BI_RGB;

                    // 绘制图像
                    SetDIBitsToDevice(hdcMem, 0, 0, imgWidth, imgHeight, 0, 0, 0, imgHeight,
                                     &pItem->imageData[0], &bmi, DIB_RGB_COLORS);

                    DeleteDC(hdcMem);
                    ReleaseDC(hwnd, hdc);
                }
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            if (hBitmap) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;

                // 计算缩放比例以居中显示
                float scaleX = (float)winWidth / imgWidth;
                float scaleY = (float)winHeight / imgHeight;
                float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.9f; // 留10%边距

                int displayWidth = (int)(imgWidth * scale);
                int displayHeight = (int)(imgHeight * scale);
                int x = (winWidth - displayWidth) / 2;
                int y = (winHeight - displayHeight) / 2;

                // 绘制位图
                HDC hdcMem = CreateCompatibleDC(hdc);
                SelectObject(hdcMem, hBitmap);
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, x, y, displayWidth, displayHeight, hdcMem, 0, 0, imgWidth, imgHeight, SRCCOPY);
                DeleteDC(hdcMem);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_SPACE) {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (hBitmap) {
                DeleteObject(hBitmap);
                hBitmap = NULL;
            }
            return 0;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
}

// 显示图像预览窗口
void ShowImagePreview(HWND hwndParent, const ClipboardItem& item) {
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ImagePreviewProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"ImagePreviewClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        classRegistered = true;
    }

    // Toggle 切换：若已有预览窗口存在，则关闭它而非再开（避免空格键
    // 反复打开新窗口，让空格键在"打开/关闭"间切换）
    HWND existing = FindWindowW(L"ImagePreviewClass", NULL);
    if (existing && IsWindow(existing)) {
        PostMessageW(existing, WM_CLOSE, 0, 0);
        return;
    }

    // 创建全屏预览窗口
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hwndPreview = CreateWindowExW(
        WS_EX_TOPMOST, L"ImagePreviewClass", T(STR_IMAGE_PREVIEW_TITLE),
        WS_POPUP | WS_VISIBLE, 0, 0, screenWidth, screenHeight, hwndParent,
        NULL, GetModuleHandle(NULL), (LPVOID)&item);
    if (hwndPreview)
      SetWindowPos(hwndPreview, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    SetForegroundWindow(hwndPreview);
}
