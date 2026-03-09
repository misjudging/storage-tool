#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <filesystem>
#include <queue>
#include <string>
#include <vector>

namespace fs = std::filesystem;

constexpr int IDC_TOP_LABEL = 100;
constexpr int IDC_TOP_EDIT = 101;
constexpr int IDC_DRIVE_BASE = 1000;
constexpr int IDC_LISTVIEW = 2000;
constexpr int IDC_STATUS = 3000;
constexpr UINT WM_SCAN_DONE = WM_APP + 1;

struct FileEntry {
    std::wstring path;
    std::uintmax_t size;
};

struct ScanResult {
    std::wstring drive;
    std::vector<FileEntry> files;
    std::wstring error;
};

struct AppState {
    HWND hwnd{};
    HWND topEdit{};
    HWND listView{};
    HWND statusLabel{};
    std::vector<HWND> driveButtons;
    bool scanning{false};
};

AppState g_app;

std::wstring format_size(std::uintmax_t size) {
    static const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(size);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    wchar_t buf[64];
    if (unit == 0) {
        swprintf_s(buf, L"%llu %s", static_cast<unsigned long long>(size), units[unit]);
    } else {
        swprintf_s(buf, L"%.1f %s", value, units[unit]);
    }
    return buf;
}

void set_controls_enabled(bool enabled) {
    EnableWindow(g_app.topEdit, enabled ? TRUE : FALSE);
    for (HWND btn : g_app.driveButtons) {
        EnableWindow(btn, enabled ? TRUE : FALSE);
    }
}

std::vector<std::wstring> get_drives() {
    std::vector<std::wstring> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) {
            wchar_t letter = static_cast<wchar_t>(L'A' + i);
            std::wstring drive;
            drive.push_back(letter);
            drive += L":\\";
            drives.push_back(drive);
        }
    }
    return drives;
}

void listview_setup_columns(HWND listView) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = const_cast<LPWSTR>(L"File");
    col.cx = 760;
    col.iSubItem = 0;
    ListView_InsertColumn(listView, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Size");
    col.cx = 160;
    col.iSubItem = 1;
    ListView_InsertColumn(listView, 1, &col);
}

void populate_results(const std::vector<FileEntry>& files) {
    ListView_DeleteAllItems(g_app.listView);
    for (size_t i = 0; i < files.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(files[i].path.c_str());
        ListView_InsertItem(g_app.listView, &item);
        std::wstring sizeText = format_size(files[i].size);
        ListView_SetItemText(g_app.listView, static_cast<int>(i), 1, sizeText.data());
    }
}

std::vector<FileEntry> scan_top_files(const std::wstring& drive, int topN, std::wstring& err) {
    struct HeapCmp {
        bool operator()(const FileEntry& a, const FileEntry& b) const {
            return a.size > b.size;
        }
    };

    std::priority_queue<FileEntry, std::vector<FileEntry>, HeapCmp> heap;

    std::error_code ec;
    fs::recursive_directory_iterator it(
        fs::path(drive),
        fs::directory_options::skip_permission_denied,
        ec
    );

    if (ec) {
        err = L"Could not access selected drive.";
        return {};
    }

    for (const auto& entry : it) {
        std::error_code statusEc;
        if (!entry.is_regular_file(statusEc) || statusEc) {
            continue;
        }

        std::error_code sizeEc;
        auto size = entry.file_size(sizeEc);
        if (sizeEc) {
            continue;
        }

        FileEntry candidate{entry.path().wstring(), static_cast<std::uintmax_t>(size)};
        if (static_cast<int>(heap.size()) < topN) {
            heap.push(std::move(candidate));
        } else if (candidate.size > heap.top().size) {
            heap.pop();
            heap.push(std::move(candidate));
        }
    }

    std::vector<FileEntry> result;
    result.reserve(heap.size());
    while (!heap.empty()) {
        result.push_back(std::move(heap.top()));
        heap.pop();
    }
    std::sort(result.begin(), result.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.size > b.size;
    });
    return result;
}

DWORD WINAPI scan_worker(LPVOID param) {
    auto* request = static_cast<std::pair<std::wstring, int>*>(param);
    auto* out = new ScanResult{};
    out->drive = request->first;
    out->files = scan_top_files(request->first, request->second, out->error);
    delete request;
    PostMessageW(g_app.hwnd, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(out));
    return 0;
}

void start_scan(const std::wstring& drive) {
    if (g_app.scanning) {
        return;
    }

    wchar_t buf[16];
    GetWindowTextW(g_app.topEdit, buf, 16);
    int topN = _wtoi(buf);
    if (topN < 1) {
        MessageBoxW(g_app.hwnd, L"Top files must be at least 1.", L"Invalid Value", MB_ICONWARNING);
        return;
    }

    g_app.scanning = true;
    set_controls_enabled(false);
    ListView_DeleteAllItems(g_app.listView);
    SetWindowTextW(g_app.statusLabel, (L"Scanning " + drive + L" ...").c_str());

    auto* payload = new std::pair<std::wstring, int>(drive, topN);
    CreateThread(nullptr, 0, scan_worker, payload, 0, nullptr);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwnd = hwnd;

            HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            CreateWindowW(
                L"STATIC",
                L"Storage Tool",
                WS_CHILD | WS_VISIBLE,
                16, 12, 300, 26,
                hwnd,
                nullptr,
                nullptr,
                nullptr
            );

            CreateWindowW(
                L"STATIC",
                L"Top files:",
                WS_CHILD | WS_VISIBLE,
                16, 44, 68, 22,
                hwnd,
                reinterpret_cast<HMENU>(IDC_TOP_LABEL),
                nullptr,
                nullptr
            );

            g_app.topEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                L"EDIT",
                L"20",
                WS_CHILD | WS_VISIBLE | ES_NUMBER,
                86, 42, 72, 24,
                hwnd,
                reinterpret_cast<HMENU>(IDC_TOP_EDIT),
                nullptr,
                nullptr
            );

            auto drives = get_drives();
            int x = 170;
            int y = 40;
            for (size_t i = 0; i < drives.size(); ++i) {
                HWND btn = CreateWindowW(
                    L"BUTTON",
                    drives[i].c_str(),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    x, y, 58, 28,
                    hwnd,
                    reinterpret_cast<HMENU>(IDC_DRIVE_BASE + static_cast<int>(i)),
                    nullptr,
                    nullptr
                );
                g_app.driveButtons.push_back(btn);
                x += 64;
            }

            g_app.listView = CreateWindowExW(
                WS_EX_CLIENTEDGE,
                WC_LISTVIEWW,
                L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                16, 80, 900, 470,
                hwnd,
                reinterpret_cast<HMENU>(IDC_LISTVIEW),
                nullptr,
                nullptr
            );
            ListView_SetExtendedListViewStyle(g_app.listView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            listview_setup_columns(g_app.listView);

            g_app.statusLabel = CreateWindowW(
                L"STATIC",
                L"Choose a drive button to start scanning.",
                WS_CHILD | WS_VISIBLE,
                16, 560, 900, 22,
                hwnd,
                reinterpret_cast<HMENU>(IDC_STATUS),
                nullptr,
                nullptr
            );

            SendMessageW(g_app.topEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(g_app.listView, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(g_app.statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id >= IDC_DRIVE_BASE && id < IDC_DRIVE_BASE + 26) {
                int idx = id - IDC_DRIVE_BASE;
                if (idx >= 0 && idx < static_cast<int>(g_app.driveButtons.size())) {
                    wchar_t text[8]{};
                    GetWindowTextW(g_app.driveButtons[idx], text, 8);
                    start_scan(text);
                }
            }
            return 0;
        }
        case WM_SCAN_DONE: {
            auto* result = reinterpret_cast<ScanResult*>(lParam);
            g_app.scanning = false;
            set_controls_enabled(true);

            if (!result->error.empty()) {
                SetWindowTextW(g_app.statusLabel, (L"Scan failed: " + result->error).c_str());
                MessageBoxW(hwnd, result->error.c_str(), L"Scan Error", MB_ICONERROR);
            } else {
                populate_results(result->files);
                std::wstring status = L"Done. Showing ";
                status += std::to_wstring(result->files.size());
                status += L" largest files on ";
                status += result->drive;
                SetWindowTextW(g_app.statusLabel, status.c_str());
            }

            delete result;
            return 0;
        }
        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;
            MoveWindow(g_app.listView, 16, 80, width - 32, height - 118, TRUE);
            MoveWindow(g_app.statusLabel, 16, height - 30, width - 32, 22, TRUE);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    const wchar_t CLASS_NAME[] = L"StorageToolWindowClass";

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Storage Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 680,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
