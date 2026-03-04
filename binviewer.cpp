#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

// Control IDs
#define ID_BTN_OPEN 1001
#define ID_RAD_HEX 1002
#define ID_RAD_BIN 1003
#define ID_RAD_OCT 1008
#define ID_RAD_DEC 1009
#define ID_RAD_ASC 1010
#define ID_TXT_BPL 1004
#define ID_LIST_OUT 1005
#define ID_LBL_BPL 1006
#define ID_BTN_UPDATE 1007

HWND hListOut, hBtnOpen, hRadHex, hRadBin, hRadOct, hRadDec, hRadAsc, hTxtBpl, hLblBpl, hBtnUpdate;
std::vector<unsigned char> fileData;
enum ViewMode { MODE_HEX, MODE_BIN, MODE_OCT, MODE_DEC, MODE_ASC };
ViewMode currentMode = MODE_HEX;
int bytesPerLine = 16;

HFONT hFont;
HFONT hUIRegularFont;
std::string headerText;
HWND hHeaderCtrl = NULL;
int colFieldWidth = 3; // unified column field width used in BOTH header and data rows

// Subclass proc for the ListView header: handles WM_PAINT to draw headerText without any length limit.
LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
        DrawEdge(hdc, &rc, EDGE_RAISED, BF_BOTTOM);
        if (hFont) SelectObject(hdc, hFont);
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkMode(hdc, TRANSPARENT);
        RECT textRc = rc;
        textRc.left += 5;
        textRc.top  += 1;
        ExtTextOutA(hdc, textRc.left, textRc.top, ETO_CLIPPED, &rc, headerText.c_str(), (int)headerText.length(), NULL);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void RebuildHeader() {
    // Step 1: compute unified field width = max(natural data width, digits needed for column index)
    int naturalDataWidth;
    switch(currentMode) {
        case MODE_HEX: naturalDataWidth = 3; break; // "FF "
        case MODE_BIN: naturalDataWidth = 9; break; // "11111111 "
        case MODE_OCT: naturalDataWidth = 4; break; // "377 "
        case MODE_DEC: naturalDataWidth = 4; break; // "255 "
        case MODE_ASC: naturalDataWidth = 2; break; // "A " (char + space)
        default:       naturalDataWidth = 3;
    }
    // Count digits in (bytesPerLine - 1)
    int colNumDigits = 1;
    for (int x = bytesPerLine - 1; x >= 10; x /= 10) colNumDigits++;
    // colFieldWidth = max(naturalDataWidth, colNumDigits + 1 space)
    colFieldWidth = (naturalDataWidth > colNumDigits + 1) ? naturalDataWidth : colNumDigits + 1;

    // Step 2: build header string using colFieldWidth for each column
    headerText = "Offset    "; // 10 chars, matches "%08X: "
    char buf[64];
    for (int j = 0; j < bytesPerLine; ++j) {
        sprintf(buf, "%-*d", colFieldWidth, j);
        headerText += buf;
    }
    headerText += " | Data"; // No more trailing ASCII section; just close the header

    if (!hListOut || !hFont) return;

    // Step 3: update column width to fit the header text
    LVCOLUMNA lc;
    lc.mask = LVCF_WIDTH;
    HDC hdc = GetDC(hListOut);
    SelectObject(hdc, hFont);
    SIZE sz;
    GetTextExtentPoint32A(hdc, headerText.c_str(), (int)headerText.length(), &sz);
    ReleaseDC(hListOut, hdc);
    lc.cx = sz.cx + 40;
    ListView_SetColumn(hListOut, 0, &lc);

    // Step 4: trigger repaint of subclassed header
    if (hHeaderCtrl) InvalidateRect(hHeaderCtrl, NULL, TRUE);
}

void UpdateDisplay() {
    char bplStr[16];
    GetWindowTextA(hTxtBpl, bplStr, sizeof(bplStr));
    int newBpl = atoi(bplStr);
    if(newBpl > 0 && newBpl <= 9999) {
        bytesPerLine = newBpl;
    } else {
        sprintf(bplStr, "%d", bytesPerLine);
        SetWindowTextA(hTxtBpl, bplStr);
    }

    if (SendMessage(hRadHex, BM_GETCHECK, 0, 0) == BST_CHECKED) currentMode = MODE_HEX;
    else if (SendMessage(hRadBin, BM_GETCHECK, 0, 0) == BST_CHECKED) currentMode = MODE_BIN;
    else if (SendMessage(hRadOct, BM_GETCHECK, 0, 0) == BST_CHECKED) currentMode = MODE_OCT;
    else if (SendMessage(hRadDec, BM_GETCHECK, 0, 0) == BST_CHECKED) currentMode = MODE_DEC;
    else if (SendMessage(hRadAsc, BM_GETCHECK, 0, 0) == BST_CHECKED) currentMode = MODE_ASC;

    RebuildHeader(); // Always rebuild header

    if (fileData.empty()) {
        ListView_SetItemCountEx(hListOut, 0, LVSICF_NOINVALIDATEALL);
        InvalidateRect(hListOut, NULL, TRUE);
        return;
    }

    size_t itemCount = (fileData.size() + bytesPerLine - 1) / bytesPerLine;
    // Force a full repaint: clear first, then reset count, then synchronous redraw
    ListView_SetItemCountEx(hListOut, 0, LVSICF_NOINVALIDATEALL);
    ListView_SetItemCountEx(hListOut, (int)itemCount, LVSICF_NOSCROLL);
    InvalidateRect(hListOut, NULL, TRUE);
    UpdateWindow(hListOut); // Force synchronous repaint so all rows are immediately re-drawn
}

        // No notify handling needed for text since we draw it ourselves

void OpenFileDlg(HWND hwnd) {
    OPENFILENAMEA ofn;
    char szFileName[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Bin Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "bin";

    if (GetOpenFileNameA(&ofn)) {
        FILE* f = fopen(szFileName, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            fileData.resize(fsize);
            fread(fileData.data(), 1, fsize, f);
            fclose(f);
            UpdateDisplay();
            SetWindowTextA(hwnd, (std::string("Binary Viewer - ") + szFileName).c_str());
        } else {
            MessageBox(hwnd, "Cannot open file", "Error", MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC  = ICC_LISTVIEW_CLASSES;
            InitCommonControlsEx(&icex);
            
            hUIRegularFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
            if (!hUIRegularFont) hUIRegularFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            hBtnOpen = CreateWindow("BUTTON", "Open File", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 10, 80, 25, hwnd, (HMENU)ID_BTN_OPEN, NULL, NULL);

            hRadHex = CreateWindow("BUTTON", "Hex", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
                110, 12, 48, 20, hwnd, (HMENU)ID_RAD_HEX, NULL, NULL);
            hRadBin = CreateWindow("BUTTON", "Bin", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                162, 12, 48, 20, hwnd, (HMENU)ID_RAD_BIN, NULL, NULL);
            hRadOct = CreateWindow("BUTTON", "Oct", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                214, 12, 48, 20, hwnd, (HMENU)ID_RAD_OCT, NULL, NULL);
            hRadDec = CreateWindow("BUTTON", "Dec", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                266, 12, 48, 20, hwnd, (HMENU)ID_RAD_DEC, NULL, NULL);
            hRadAsc = CreateWindow("BUTTON", "ASCII", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
                318, 12, 55, 20, hwnd, (HMENU)ID_RAD_ASC, NULL, NULL);
            SendMessage(hRadHex, BM_SETCHECK, BST_CHECKED, 0);

            hLblBpl = CreateWindow("STATIC", "Bytes/line:", WS_VISIBLE | WS_CHILD,
                385, 14, 70, 20, hwnd, (HMENU)ID_LBL_BPL, NULL, NULL);
            hTxtBpl = CreateWindow("EDIT", "16", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER,
                455, 12, 50, 22, hwnd, (HMENU)ID_TXT_BPL, NULL, NULL);

            hBtnUpdate = CreateWindow("BUTTON", "Apply", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                515, 10, 60, 25, hwnd, (HMENU)ID_BTN_UPDATE, NULL, NULL);

            SendMessage(hBtnOpen, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hRadHex, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hRadBin, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hRadOct, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hRadDec, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hRadAsc, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hLblBpl, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hTxtBpl, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);
            SendMessage(hBtnUpdate, WM_SETFONT, (WPARAM)hUIRegularFont, TRUE);

            hListOut = CreateWindow(WC_LISTVIEW, "", WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_OWNERDRAWFIXED | LVS_NOSORTHEADER | LVS_SINGLESEL,
                10, 45, 760, 505, hwnd, (HMENU)ID_LIST_OUT, NULL, NULL);
            ListView_SetExtendedListViewStyle(hListOut, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

            LVCOLUMNA lc;
            lc.mask = LVCF_TEXT | LVCF_WIDTH;
            lc.pszText = (LPSTR)"File Content";
            lc.cx = 800;
            ListView_InsertColumn(hListOut, 0, &lc);

            hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(hListOut, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            hHeaderCtrl = ListView_GetHeader(hListOut);
            SendMessage(hHeaderCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
            // Subclass the header so we can paint unlimited-length text
            SetWindowSubclass(hHeaderCtrl, HeaderSubclassProc, 1, 0);

            // Initialize header with default mode+bpl so column numbers show immediately
            RebuildHeader();
            break;
        }
        case WM_NOTIFY: {
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
            if (wParam == ID_LIST_OUT) {
                if (lpdis->itemID == (UINT)-1) break;
                
                size_t offset = (size_t)lpdis->itemID * bytesPerLine;
                if (offset >= fileData.size()) return TRUE;
                
                // Use std::string to safely handle any number of bytes per line
                std::string outStr;
                outStr.reserve(10 + (size_t)bytesPerLine * colFieldWidth + 3 + bytesPerLine);
                
                char tmp[32];
                sprintf(tmp, "%08X: ", (unsigned int)offset);
                outStr += tmp;
                
                for (int j = 0; j < bytesPerLine; ++j) {
                    if (offset + j < fileData.size()) {
                        unsigned char c = fileData[offset + j];
                        char valStr[16];
                        int valLen = 0;
                        if (currentMode == MODE_HEX) {
                            valLen = sprintf(valStr, "%02X", (int)c);
                        } else if (currentMode == MODE_BIN) {
                            for(int b=7; b>=0; --b)
                                valStr[valLen++] = ((c >> b) & 1) ? '1' : '0';
                            valStr[valLen] = '\0';
                        } else if (currentMode == MODE_OCT) {
                            valLen = sprintf(valStr, "%03o", (int)c);
                        } else if (currentMode == MODE_DEC) {
                            valLen = sprintf(valStr, "%03d", (int)c);
                        } else if (currentMode == MODE_ASC) {
                            valStr[0] = (c >= 32 && c <= 126) ? (char)c : '.';
                            valStr[1] = '\0';
                            valLen = 1;
                        }
                        outStr += valStr;
                        for (int p = valLen; p < colFieldWidth; ++p) outStr += ' ';
                    } else {
                        for (int p = 0; p < colFieldWidth; ++p) outStr += ' ';
                    }
                }
                // No trailing ASCII section - removed, ASCII is now a dedicated mode
                
                if (lpdis->itemState & ODS_SELECTED) {
                    SetTextColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    SetBkColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHT));
                    FillRect(lpdis->hDC, &lpdis->rcItem, GetSysColorBrush(COLOR_HIGHLIGHT));
                } else {
                    SetTextColor(lpdis->hDC, GetSysColor(COLOR_WINDOWTEXT));
                    SetBkColor(lpdis->hDC, GetSysColor(COLOR_WINDOW));
                    FillRect(lpdis->hDC, &lpdis->rcItem, GetSysColorBrush(COLOR_WINDOW));
                }
                
                SelectObject(lpdis->hDC, hFont);
                RECT rc = lpdis->rcItem;
                rc.left += 5;
                ExtTextOutA(lpdis->hDC, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &lpdis->rcItem,
                            outStr.c_str(), (int)outStr.length(), NULL);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_BTN_OPEN) {
                OpenFileDlg(hwnd);
            } else if (LOWORD(wParam) == ID_BTN_UPDATE || (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == ID_RAD_HEX || LOWORD(wParam) == ID_RAD_BIN || LOWORD(wParam) == ID_RAD_OCT || LOWORD(wParam) == ID_RAD_DEC || LOWORD(wParam) == ID_RAD_ASC))) {
                UpdateDisplay();
            }
            break;
        }
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            MoveWindow(hListOut, 10, 45, width - 20, height - 55, TRUE);
            break;
        }
        case WM_DESTROY:
            DeleteObject(hFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc;
    HWND hwnd;
    MSG Msg;

    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = "BinViewerClass";
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    hwnd = CreateWindowEx(
        0,
        "BinViewerClass",
        "Binary Viewer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);

    if(hwnd == NULL) {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while(GetMessage(&Msg, NULL, 0, 0) > 0) {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return Msg.wParam;
}
