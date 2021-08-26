#include "pch.h"
#include "framework.h"
#include "resource.h"

#include <stdio.h>
#include <windows.h>
#include <windowsx.h>
#include <ShObjIdl.h>
#include <omp.h>
#include <iostream>
#include <fstream>
#include <string>
#include <queue>
#include <tuple>
using namespace std;

WNDCLASS main_wc, image_wc, regions_wc;
ATOM main_wClass, image_wClass, regions_wClass;
HWND main_hwnd, image_hwnd, regions_hwnd;
HWND open_button_hwnd, start_cancel_button_hwnd, close_button_hwnd, save_button_hwnd;
HWND same_level_label_hwnd, same_level_hwnd, start_point_mode_button_hwnd;
HWND threads_count_label_hwnd, threads_count_button_hwnd[5];
HWND cycle_speed_button_hwnd;

int width, height, colors;
uint32_t *image_pixels, *image_region_pixels, *image_pixels_visited, *image_pixels_visited_local, *regions_pixels;
size_t memory_size;
BITMAPINFO bitmapinfo;
#define PIX(m,x,y) ((m)[(height - (y) - 1) * width + (x)])
#define PIXCOMP(p,i) (((unsigned char*)&(p))[(2-i)])
#define SAME(c1,c2) (color_distance((c1), (c2)) <= same_level)
bool opened = false, working = false, pause = false, finished = false;

#define THREADS_COUNT_0 0
#define THREADS_COUNT_1 1
#define THREADS_COUNT_2 2
#define THREADS_COUNT_3 3
#define THREADS_COUNT_4 4
#define OPEN_BUTTON 5
#define START_CANCEL_BUTTON 6
#define CLOSE_BUTTON 7
#define SAVE_BUTTON 8
#define START_POINT_MODE 9
#define CYCLE_SPEED_BUTTON 10
using point = pair<int, int>;

int threads_count = 1;
int speed = 1000;
int same_level = 1;
int start_point_mode = 2;

double color_distance(int c1, int c2)
{
    int r1 = PIXCOMP(c1, 0), g1 = PIXCOMP(c1, 1), b1 = PIXCOMP(c1, 2);
    int r2 = PIXCOMP(c2, 0), g2 = PIXCOMP(c2, 1), b2 = PIXCOMP(c2, 2);
    long rmean = (r1 + r2) / 2;
    long dr = r1 - r2;
    long dg = g1 - g2;
    long db = b1 - b1;
    return sqrt((((512 + rmean) * dr * dr) >> 8) + 4 * dg * dg + (((767 - rmean) * db * db) >> 8));
}

int colors_hex[4] = { 0x00ff00, 0xff0000, 0x0000ff, 0xffff00 };
queue<point> border_candidates[4], candidates[4];
void search_quarter(int quarter, int start_x, int start_y, int color, int _threads_count, int _speed) {
    int dsk = 0;

    queue<point> local_border_candidates[4];
    int foreign_right = 999, foreign_up = 999, foreign_left = 999, foreign_down = 999;
    int x_begin = 0, x_end = width - 1, y_begin = 0, y_end = height - 1;
    if (_threads_count > 0)
        switch (quarter) {
        case 0: x_begin = ++start_x; y_end = start_y; foreign_left = 1; foreign_down = 3; break;
        case 1: x_end = start_x; y_end = --start_y; foreign_down = 2; foreign_right = 0; break;
        case 2: x_end = --start_x; y_begin = start_y; foreign_up = 1; foreign_right = 3; break;
        case 3: x_begin = start_x; y_begin = ++start_y; foreign_up = 0; foreign_left = 2; break;
        }
    if (start_x >= 0 && start_y >= 0 && start_x < width && start_y < height) 
        candidates[quarter].emplace(start_x, start_y);

    do {
        #pragma omp barrier

        while (!candidates[quarter].empty()) {
            auto candidate = candidates[quarter].front(); candidates[quarter].pop();
            int x = candidate.first, y = candidate.second;
            PIX(image_region_pixels, x, y) = colors_hex[quarter];

            if (x < x_end) {
                if (SAME(PIX(image_pixels, x + 1, y), color) && !PIX(image_pixels_visited, x + 1, y)) {
                    candidates[quarter].emplace(x + 1, y);
                    PIX(image_pixels_visited_local, x + 1, y) = PIX(image_pixels_visited, x + 1, y) = true;
                }
            }
            else if (x < width - 1 && SAME(PIX(image_pixels, x + 1, y), color))
                local_border_candidates[foreign_right].emplace(x + 1, y);

            if (y > y_begin) {
                if (SAME(PIX(image_pixels, x, y - 1), color) && !PIX(image_pixels_visited, x, y - 1)) {
                    candidates[quarter].emplace(x, y - 1);
                    PIX(image_pixels_visited_local, x, y - 1) = PIX(image_pixels_visited, x, y - 1) = true;
                }
            }
            else if (y > 0 && SAME(PIX(image_pixels, x, y - 1), color))
                local_border_candidates[foreign_up].emplace(x, y - 1);

            if (x > x_begin) {
                if (SAME(PIX(image_pixels, x - 1, y), color) && !PIX(image_pixels_visited, x - 1, y)) {
                    candidates[quarter].emplace(x - 1, y);
                    PIX(image_pixels_visited_local, x - 1, y) = PIX(image_pixels_visited, x - 1, y) = true;
                }
            }
            else if (x > 0 && SAME(PIX(image_pixels, x - 1, y), color))
                local_border_candidates[foreign_left].emplace(x - 1, y);

            if (y < y_end) {
                if (SAME(PIX(image_pixels, x, y + 1), color) && !PIX(image_pixels_visited, x, y + 1)) {
                    candidates[quarter].emplace(x, y + 1);
                    PIX(image_pixels_visited_local, x, y + 1) = PIX(image_pixels_visited, x, y + 1) = true;
                }
            }
            else if (y < height - 1 && SAME(PIX(image_pixels, x, y + 1), color))
                local_border_candidates[foreign_down].emplace(x, y + 1);

            InvalidateRect(image_hwnd, NULL, NULL);
            if (_speed != 0 && ++dsk % _speed == 0) Sleep(1);
        }

        #pragma omp barrier

        #pragma omp critical
        {
            for (int q = 0; q < 4; ++q)
                while (!local_border_candidates[q].empty()) {
                    border_candidates[q].push(local_border_candidates[q].front());
                    local_border_candidates[q].pop();
                }
        }

        #pragma omp barrier

        while (!border_candidates[quarter].empty()) {
            auto candidate = border_candidates[quarter].front(); border_candidates[quarter].pop();
            int x = candidate.first, y = candidate.second;
            if (!PIX(image_pixels_visited, x, y))
                candidates[quarter].push(candidate);
        }

        #pragma omp barrier
    } while (!candidates[0].empty() || !candidates[1].empty() || !candidates[2].empty() || !candidates[3].empty());
}

point pick_start_point(int mode) {
    int start_x, start_y;
    int start_points_count = 0;
    for (start_y = 0; start_y < height; ++start_y)
        for (start_x = 0; start_x < width; ++start_x)
            if (!PIX(image_pixels_visited, start_x, start_y)) {
                ++start_points_count;
                if (mode == 0) {
                    wchar_t _buf[100];
                    wsprintf(_buf, L"Регіони: %d%% виконано", start_y * 100 / height);
                    SetWindowText(regions_hwnd, _buf);
                    return make_pair(start_x, start_y);
                }
            }

    if (mode != 0) {
        wchar_t _buf[100];
        wsprintf(_buf, L"Регіони: %d%% виконано", (width * height - start_points_count) * 100 / (width * height));
        SetWindowText(regions_hwnd, _buf);
    }

    if (start_points_count == 0)
        return make_pair(-1, -1);

    int cur_num = 0, start_point_num = (int)(start_points_count * (mode == 1 ? (rand() / (float)RAND_MAX) : 0.5f)) % start_points_count;
    for (start_y = 0; start_y < height; ++start_y)
        for (start_x = 0; start_x < width; ++start_x)
            if (!PIX(image_pixels_visited, start_x, start_y))
                if (cur_num++ == start_point_num)
                    return make_pair(start_x, start_y);

    return make_pair(-1, -1);
}

#define MAX_THREADS 4
HANDLE search_thread;
bool close, stop;
omp_lock_t params_lock;
DWORD WINAPI search_thread_main(CONST LPVOID lpParam) {
    memcpy(image_region_pixels, image_pixels, memory_size);
    InvalidateRect(regions_hwnd, NULL, NULL);
    memset(regions_pixels, 0, memory_size);
    memset(image_pixels_visited, 0, memory_size);
    
    while (true) {
        memset(image_pixels_visited_local, 0, memory_size);
        omp_set_lock(&params_lock);
        int _threads_count = threads_count;
        int _speed = speed;
        bool _stop = stop;
        int _start_point_mode = start_point_mode;
        omp_unset_lock(&params_lock);
        if (stop)
            break;

        point start_point = pick_start_point(_start_point_mode);
        int start_x = start_point.first, start_y = start_point.second;

        if (start_x < 0 && start_y < 0) break;
    
        memcpy(image_region_pixels, image_pixels, memory_size);
        int start_color = PIX(image_pixels, start_x, start_y);
        PIX(image_pixels_visited_local, start_x, start_y) = PIX(image_pixels_visited, start_x, start_y) = true;
        PIX(regions_pixels, start_x, start_y) = PIX(image_pixels, start_x, start_y);

        for (int i = 0; i < 4; ++i)
            queue<point>().swap(border_candidates[i]);

        #pragma omp parallel num_threads(max(_threads_count, 1))
        search_quarter(omp_get_thread_num(), start_x, start_y, start_color, _threads_count, _speed);

        for (int x = 0; x < width; ++x)
            for (int y = 0; y < width; ++y)
                if (PIX(image_pixels_visited_local, x, y) && PIX(regions_pixels, x, y) == 0)
                    PIX(regions_pixels, x, y) = start_color;
        InvalidateRect(regions_hwnd, NULL, NULL);
    }

    finished = true;
    SetWindowText(regions_hwnd, L"Регіони");
    SendMessage(main_hwnd, WM_NOTIFY, NULL, NULL);
    return 0;
}

void refresh_state() {
    EnableWindow(open_button_hwnd, !working);
    EnableWindow(start_cancel_button_hwnd, opened);
    SetWindowText(start_cancel_button_hwnd, working ? L"Відмінити" : L"Запуск");
    ShowWindow(close_button_hwnd, opened ? SW_SHOW : SW_HIDE);
    EnableWindow(same_level_hwnd, !working ? SW_SHOW : SW_HIDE);
    ShowWindow(threads_count_label_hwnd, opened ? SW_SHOW : SW_HIDE);
    for (int i = 0; i <= 4; ++i)
        ShowWindow(threads_count_button_hwnd[i], opened ? SW_SHOW : SW_HIDE);
    ShowWindow(cycle_speed_button_hwnd, opened ? SW_SHOW : SW_HIDE);

    ShowWindow(image_hwnd, opened ? SW_SHOW : SW_HIDE);
    ShowWindow(regions_hwnd, opened && (working || finished) ? SW_SHOW : SW_HIDE);
    ShowWindow(same_level_label_hwnd, opened ? SW_SHOW : SW_HIDE);
    ShowWindow(start_point_mode_button_hwnd, opened ? SW_SHOW : SW_HIDE);
    ShowWindow(same_level_hwnd, opened ? SW_SHOW : SW_HIDE);
    ShowWindow(save_button_hwnd, finished ? SW_SHOW : SW_HIDE);
}

void finish_close() {
    if (stop) finished = false;
    working = false;
    opened = !stop || !close;
    refresh_state();

    if (close) {
        VirtualFree(regions_pixels, 0, MEM_RELEASE);
        VirtualFree(image_pixels_visited_local, 0, MEM_RELEASE);
        VirtualFree(image_pixels_visited, 0, MEM_RELEASE);
        VirtualFree(image_region_pixels, 0, MEM_RELEASE);
        VirtualFree(image_pixels, 0, MEM_RELEASE);
    }
    else {
        memcpy(image_region_pixels, image_pixels, memory_size);
        InvalidateRect(image_hwnd, NULL, NULL);
    }
}

void close_ppm(bool _close) {
    if (!opened) return;
    omp_set_lock(&params_lock);
    close = _close;
    stop = true;
    if (!working)
        finish_close();
    finished = false;
    omp_unset_lock(&params_lock);
}

PWSTR open_file_dialog(COMDLG_FILTERSPEC* file_types, int file_types_count) {
    PWSTR pszFilePath = NULL;
    IFileOpenDialog* pFileOpen;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
        IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    if (!SUCCEEDED(hr)) return NULL;
    hr = pFileOpen->SetFileTypes(file_types_count, file_types);
    if (!SUCCEEDED(hr)) return NULL;
    EnableWindow(open_button_hwnd, false);
    hr = pFileOpen->Show(NULL);
    EnableWindow(open_button_hwnd, true);
    if (!SUCCEEDED(hr)) return NULL;
    IShellItem* pItem;
    hr = pFileOpen->GetResult(&pItem);
    if (!SUCCEEDED(hr)) return NULL;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    pItem->Release();
    pFileOpen->Release();
    return pszFilePath;
}

PWSTR save_file_dialog(COMDLG_FILTERSPEC* file_types, int file_types_count) {
    PWSTR pszFilePath = NULL;
    IFileSaveDialog* pFileSave;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
        IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileSave));
    if (!SUCCEEDED(hr)) return NULL;
    hr = pFileSave->SetFileTypes(file_types_count, file_types);
    if (!SUCCEEDED(hr)) return NULL;
    DWORD dwFlags;
    hr = pFileSave->GetOptions(&dwFlags);
    if (!SUCCEEDED(hr)) return NULL;
    hr = pFileSave->SetOptions(dwFlags & ~FOS_FILEMUSTEXIST);
    if (!SUCCEEDED(hr)) return NULL;
    hr = pFileSave->SetDefaultExtension(L"ppm");
    if (!SUCCEEDED(hr)) return NULL;
    EnableWindow(save_button_hwnd, false);
    hr = pFileSave->Show(NULL);
    EnableWindow(save_button_hwnd, true);
    if (!SUCCEEDED(hr)) return NULL;
    IShellItem* pItem;
    hr = pFileSave->GetResult(&pItem);
    if (!SUCCEEDED(hr)) return NULL;
    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
    if (!SUCCEEDED(hr)) return NULL;
    pItem->Release();
    pFileSave->Release();
    return pszFilePath;
}

void resize_client_area(HWND hwnd, int x, int y, int height, int width) {
    RECT window_rect, client_rect;
    GetWindowRect(hwnd, &window_rect);
    GetClientRect(hwnd, &client_rect);
    SetWindowPos(hwnd, NULL, x, y,
        height + (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left),
        width + (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top),
        NULL);
}

void set_param_threads_count(int new_count) {
    omp_set_lock(&params_lock);
    threads_count = new_count;
    omp_unset_lock(&params_lock);

    wchar_t _buf[100];
    wsprintf(_buf, L"Потоків: %d", threads_count);
    SetWindowText(threads_count_label_hwnd, _buf);
}

void set_param_speed(int new_speed) {
    omp_set_lock(&params_lock);
    speed = new_speed;
    omp_unset_lock(&params_lock);

    wchar_t _buf[100];
    wsprintf(_buf, speed != 0 ? L"Швидкість: %d" : L"Швидкість: макс.", speed);
    SetWindowText(cycle_speed_button_hwnd, _buf);
}

void set_param_start_point_mode(int new_mode) {
    omp_set_lock(&params_lock);
    start_point_mode = new_mode;
    omp_unset_lock(&params_lock);

    const wchar_t* _buf[3] = { L"Старт: перший", L"Старт: випадковий", L"Старт: середній" };
    SetWindowText(start_point_mode_button_hwnd, _buf[start_point_mode]);
}

void open_ppm() {
    if (working) return;
    close_ppm(true);

    COMDLG_FILTERSPEC file_types[] = { { L"PPM-зображення", L"*.ppm" } };
    LPWSTR file_path = (wchar_t*)open_file_dialog(file_types, _countof(file_types));
    if (file_path == NULL) return;

    ifstream file(file_path, ios_base::in | ios_base::binary);
    if (!file.is_open()) {
        MessageBox(main_hwnd, L"Error opening file!",
            L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (opened) close_ppm(true);

    string _tmp;
    getline(file, _tmp);
    if (_tmp.compare("P6")) {
        MessageBox(main_hwnd, L"PPM format signature missing!",
            L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    file >> width >> height >> colors;
    if (colors != 255) {
        MessageBox(main_hwnd, L"Only 255-colors PPM are supported!",
            L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    memory_size = width * height * 4;
    image_pixels = (uint32_t*)VirtualAlloc(NULL, (SIZE_T)memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    image_region_pixels = (uint32_t*)VirtualAlloc(NULL, (SIZE_T)memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    image_pixels_visited = (uint32_t*)VirtualAlloc(NULL, (SIZE_T)memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    image_pixels_visited_local = (uint32_t*)VirtualAlloc(NULL, (SIZE_T)memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    regions_pixels = (uint32_t*)VirtualAlloc(NULL, (SIZE_T)memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    file.get();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char R = file.get();
            unsigned char G = file.get();
            unsigned char B = file.get();
            PIX(image_pixels, x, y) = (R << 16) | (G << 8) | B;
        }
    }
    file.close();

    bitmapinfo.bmiHeader.biSize = sizeof(bitmapinfo.bmiHeader);
    bitmapinfo.bmiHeader.biWidth = width;
    bitmapinfo.bmiHeader.biHeight = height;
    bitmapinfo.bmiHeader.biPlanes = 1;
    bitmapinfo.bmiHeader.biBitCount = 32;
    bitmapinfo.bmiHeader.biCompression = BI_RGB;

    opened = true;
    working = finished = false;
    close = stop = false;
    resize_client_area(image_hwnd, 5, 5, width, height);
    resize_client_area(regions_hwnd, 20 + width, 5, width, height);
    refresh_state();

    set_param_threads_count(4);
    set_param_speed(0);
    set_param_start_point_mode(1);

    memcpy(image_region_pixels, image_pixels, memory_size);
}

void save_ppm() {
    if (!finished) return;

    COMDLG_FILTERSPEC file_types[] = { { L"PPM-зображення", L"*.ppm" } };
    LPWSTR file_path = save_file_dialog(file_types, _countof(file_types));
    if (file_path == NULL) return;

    ofstream file(file_path, ios_base::out | ios_base::binary);

    if (!file.is_open()) {
        MessageBox(main_hwnd, L"Error opening file!",
            L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    file << "P6\n" << width << " " << height << "\n" << colors << "\n";

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char R = PIXCOMP(PIX(regions_pixels, x, y), 0);
            unsigned char G = PIXCOMP(PIX(regions_pixels, x, y), 1);
            unsigned char B = PIXCOMP(PIX(regions_pixels, x, y), 2);
            file.put(R); file.put(G); file.put(B);
        }
    }

    file.close();
}

void start_cancel() {
    if (working)
        close_ppm(false);
    else {
        stop = false;
        memcpy(image_region_pixels, image_pixels, memory_size);
        wchar_t _buf[4];
        GetWindowText(same_level_hwnd, _buf, 4);
        same_level = _wtoi(_buf);

        if ((search_thread = CreateThread(NULL, NULL, &search_thread_main, NULL, NULL, NULL)) == NULL) {
            MessageBox(main_hwnd, L"Error creating thread!", L"Error", MB_OK | MB_ICONERROR);
            return;
        };
        working = true;
    }
    refresh_state();
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        image_hwnd = CreateWindowW(
            MAKEINTATOM(image_wClass),
            L"Зображення",
            NULL,
            CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
            main_hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );
        if (!image_hwnd)
            return 1;

        regions_hwnd = CreateWindowW(
            MAKEINTATOM(regions_wClass),
            L"Регіони",
            NULL,
            CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
            main_hwnd,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );
        if (!regions_hwnd)
            return 1;
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        DestroyWindow(image_hwnd);
        PostQuitMessage(0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        switch (wParam)
        {
        case OPEN_BUTTON: open_ppm(); break;
        case START_CANCEL_BUTTON: start_cancel(); break;
        case CLOSE_BUTTON: close_ppm(true); break;
        case SAVE_BUTTON: save_ppm(); break;
        case START_POINT_MODE:
            set_param_start_point_mode(++start_point_mode % 3);
            break;
        case THREADS_COUNT_0: set_param_threads_count(0); break;
        case THREADS_COUNT_1: set_param_threads_count(1); break;
        case THREADS_COUNT_2: set_param_threads_count(2); break;
        case THREADS_COUNT_3: set_param_threads_count(3); break;
        case THREADS_COUNT_4: set_param_threads_count(4); break;
        case CYCLE_SPEED_BUTTON:
            switch (speed) {
            case 0: set_param_speed(100); break;
            case 100: set_param_speed(1000); break;
            case 1000:set_param_speed(0); break;
            }
        }
        break;
    }
    case WM_NOTIFY:
        if (stop || finished) finish_close();
        break;
    };
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void PaintMemory(HWND hwnd, uint32_t* memory) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    StretchDIBits(hdc, 0, 0, width, height, 0, 0, width, height, memory, &bitmapinfo, DIB_RGB_COLORS, SRCCOPY);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ImageWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg)
    {
    case WM_CLOSE:   DestroyWindow(main_hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    case WM_PAINT:   PaintMemory(hwnd, image_region_pixels);   return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK RegionsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg)
    {
    case WM_CLOSE:   DestroyWindow(main_hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0);  return 0;
    case WM_PAINT:   PaintMemory(hwnd, regions_pixels);   return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    omp_init_lock(&params_lock);

    main_wc = {
        0, MainWndProc, 0, 0, 0,
        LoadIcon(NULL, IDI_APPLICATION),
        LoadCursor(NULL, IDC_ARROW),
        NULL,
        NULL,
        L"MainWindowClass"
    };
    main_wClass = RegisterClass(&main_wc);
    if (!main_wClass)
        return 1;

    image_wc = {
        0, ImageWndProc, 0, 0, 0,
        LoadIcon(NULL, IDI_APPLICATION),
        LoadCursor(NULL, IDC_ARROW),
        NULL,
        NULL,
        L"ImageWindowClass"
    };
    image_wClass = RegisterClass(&image_wc);
    if (!image_wClass)
        return 1;

    regions_wc = {
        0, RegionsWndProc, 0, 0, 0,
        LoadIcon(NULL, IDI_APPLICATION),
        LoadCursor(NULL, IDC_ARROW),
        NULL,
        NULL,
        L"RegionsWindowClass"
    };
    regions_wClass = RegisterClass(&regions_wc);
    if (!regions_wClass)
        return 1;

    main_hwnd = CreateWindow(
        MAKEINTATOM(main_wClass),
        L"Лабораторна робота 6",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 465, 168,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    if (!main_hwnd)
        return 1;

    open_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Відкрити",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        10,
        10,
        80,
        30,
        main_hwnd,
        (HMENU)OPEN_BUTTON,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!open_button_hwnd)
        return -1;

    start_cancel_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Запуск",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        100,
        10,
        90,
        30,
        main_hwnd,
        (HMENU)START_CANCEL_BUTTON,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!open_button_hwnd)
        return -1;

    close_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Закрити",
        WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        200,
        10,
        80,
        30,
        main_hwnd,
        (HMENU)CLOSE_BUTTON,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!close_button_hwnd)
        return -1;

    save_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Зберегти",
        WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        290,
        10,
        80,
        30,
        main_hwnd,
        (HMENU)SAVE_BUTTON,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!save_button_hwnd)
        return -1;

    same_level_label_hwnd = CreateWindow(
        L"EDIT",
        L"Розкид:",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        10,
        95,
        80,
        30,
        main_hwnd,
        NULL,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!same_level_label_hwnd)
        return -1;

    same_level_hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"100",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_NUMBER,
        80,
        90,
        40,
        30,
        main_hwnd,
        NULL,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!same_level_hwnd)
        return -1;
    SendMessage(same_level_hwnd, EM_SETLIMITTEXT, 3, 0);

    start_point_mode_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Старт:",
        WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        140,
        90,
        160,
        30,
        main_hwnd,
        (HMENU)START_POINT_MODE,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!start_point_mode_button_hwnd)
        return -1;

    cycle_speed_button_hwnd = CreateWindow(
        L"BUTTON",
        L"Швидкість",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_LEFT,
        10,
        50,
        140,
        30,
        main_hwnd,
        (HMENU)CYCLE_SPEED_BUTTON,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!open_button_hwnd)
        return -1;

    threads_count_label_hwnd = CreateWindow(
        L"EDIT",
        L"Потоків:",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        160,
        55,
        80,
        30,
        main_hwnd,
        NULL,
        (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
        NULL);
    if (!threads_count_label_hwnd)
        return -1;

    const wchar_t* _threads_count_button_text[5] = { L"0", L"1", L"2", L"3", L"4" };
    const int hMenu[5] = { THREADS_COUNT_0, THREADS_COUNT_1, THREADS_COUNT_2, THREADS_COUNT_3, THREADS_COUNT_4 };
    for (int i = 0; i <= 4; ++i)
        if(!(threads_count_button_hwnd[i] = CreateWindow(
            L"BUTTON",
            _threads_count_button_text[i],
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            250 + 40 * i,
            50,
            30,
            30,
            main_hwnd,
            (HMENU) hMenu[i],
            (HINSTANCE)GetWindowLongPtr(main_hwnd, GWLP_HINSTANCE),
            NULL)))
            return -1;

    refresh_state();
    ShowWindow(main_hwnd, SW_SHOWNORMAL);

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, NULL, 0, 0) > 0) != 0)
    {
        if (bRet == -1) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    omp_destroy_lock(&params_lock);
    return (int)msg.wParam;
}