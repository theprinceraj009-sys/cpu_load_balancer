#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define NUM_CORES 4
#define LOG_LINES 4

#define ID_BTN_ADD_LIGHT 101
#define ID_BTN_ADD_HEAVY 102
#define ID_BTN_TOGGLE_GEN 103
#define ID_BTN_TOGGLE_ALGO 104

typedef enum { ALGO_LEAST_LOADED, ALGO_ROUND_ROBIN } AlgoType;

typedef struct Task {
    int id;
    int burst_time;
    int remaining_time;
    struct Task* next;
} Task;

typedef struct {
    int id;
    Task* head;
    Task* tail;
    int active_tasks;
    int total_remaining_load;
    pthread_mutex_t lock;
    pthread_t thread_id;
} CPUCore;

CPUCore cores[NUM_CORES];
volatile bool running = true;
volatile bool auto_generator_active = true;
AlgoType current_algorithm = ALGO_LEAST_LOADED;

int global_task_counter = 1;
int completed_tasks_count = 0;
int rr_current_core = 0;

// Log Buffer for Simulation Transparency
char log_buffer[LOG_LINES][128];
int log_count = 0;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

HWND hBtnLight, hBtnHeavy, hBtnToggle, hBtnAlgo;

void add_log_entry(const char* log_text) {
    pthread_mutex_lock(&log_lock);
    if (log_count < LOG_LINES) {
        strcpy(log_buffer[log_count++], log_text);
    } else {
        for (int i = 0; i < LOG_LINES - 1; i++) {
            strcpy(log_buffer[i], log_buffer[i + 1]);
        }
        strcpy(log_buffer[LOG_LINES - 1], log_text);
    }
    pthread_mutex_unlock(&log_lock);
}

void assign_task_to_core(Task* task) {
    int target_core = 0;
    char logMsg[128];

    if (current_algorithm == ALGO_LEAST_LOADED) {
        int min_load = 1e9;
        int core_loads[NUM_CORES];

        for (int i = 0; i < NUM_CORES; i++) {
            pthread_mutex_lock(&cores[i].lock);
            core_loads[i] = cores[i].total_remaining_load;
            if (core_loads[i] < min_load) {
                min_load = core_loads[i];
                target_core = i;
            }
            pthread_mutex_unlock(&cores[i].lock);
        }

        sprintf(logMsg, "[LEAST-LOADED] Task #%d (%ds) -> Loads [C0:%ds, C1:%ds, C2:%ds, C3:%ds] -> Assigned Core %d",
                task->id, task->burst_time, core_loads[0], core_loads[1], core_loads[2], core_loads[3], target_core);
    } else {
        pthread_mutex_lock(&counter_lock);
        target_core = rr_current_core;
        rr_current_core = (rr_current_core + 1) % NUM_CORES;
        pthread_mutex_unlock(&counter_lock);

        sprintf(logMsg, "[ROUND-ROBIN] Task #%d (%ds) -> Sequential Turn -> Assigned Core %d",
                task->id, task->burst_time, target_core);
    }

    add_log_entry(logMsg);

    pthread_mutex_lock(&cores[target_core].lock);
    if (cores[target_core].tail == NULL) {
        cores[target_core].head = cores[target_core].tail = task;
    } else {
        cores[target_core].tail->next = task;
        cores[target_core].tail = task;
    }
    cores[target_core].active_tasks++;
    cores[target_core].total_remaining_load += task->burst_time;
    pthread_mutex_unlock(&cores[target_core].lock);
}

void add_user_task(int burst_time) {
    Task* new_task = (Task*)malloc(sizeof(Task));
    pthread_mutex_lock(&counter_lock);
    new_task->id = global_task_counter++;
    pthread_mutex_unlock(&counter_lock);

    new_task->burst_time = burst_time;
    new_task->remaining_time = burst_time;
    new_task->next = NULL;

    assign_task_to_core(new_task);
}

void* cpu_worker(void* arg) {
    CPUCore* core = (CPUCore*)arg;
    while (running) {
        pthread_mutex_lock(&core->lock);
        Task* current = core->head;
        if (current != NULL) {
            current->remaining_time--;
            core->total_remaining_load--;

            if (current->remaining_time <= 0) {
                core->head = current->next;
                if (core->head == NULL) core->tail = NULL;
                core->active_tasks--;
                
                char doneLog[128];
                sprintf(doneLog, "[COMPLETE] Core %d finished Task #%d", core->id, current->id);
                add_log_entry(doneLog);

                free(current);

                pthread_mutex_lock(&stats_lock);
                completed_tasks_count++;
                pthread_mutex_unlock(&stats_lock);
            }
        }
        pthread_mutex_unlock(&core->lock);
        sleep(1);
    }
    return NULL;
}

void* task_generator(void* arg) {
    while (running) {
        if (auto_generator_active && global_task_counter <= 100) {
            add_user_task((rand() % 5) + 2);
            usleep((rand() % 1200 + 800) * 1000);
        } else {
            sleep(1);
        }
    }
    return NULL;
}

void DrawRoundedCard(HDC hdc, int left, int top, int right, int bottom, COLORREF bgColor, COLORREF borderColor) {
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    RoundRect(hdc, left, top, right, bottom, 10, 10);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

void DrawModernGUI(HDC hdc) {
    int totalSystemLoad = 0;
    int totalActiveTasks = 0;

    for (int i = 0; i < NUM_CORES; i++) {
        pthread_mutex_lock(&cores[i].lock);
        totalSystemLoad += cores[i].total_remaining_load;
        totalActiveTasks += cores[i].active_tasks;
        pthread_mutex_unlock(&cores[i].lock);
    }

    // Header
    HFONT hFontHeader = CreateFont(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
    SelectObject(hdc, hFontHeader);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(240, 243, 246));
    TextOutA(hdc, 30, 12, "ADVANCED CPU LOAD BALANCER", 26);

    HFONT hFontSub = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, RGB(130, 145, 162));
    TextOutA(hdc, 30, 38, "Real-Time Allocation Decision Logger | Dynamic Multithreading Engine", 69);
    DeleteObject(hFontHeader);

    // Active Algo Card
    DrawRoundedCard(hdc, 330, 12, 470, 54, RGB(28, 36, 48), RGB(45, 58, 75));
    SetTextColor(hdc, RGB(140, 155, 175));
    TextOutA(hdc, 340, 16, "ACTIVE ALGO", 11);
    
    HFONT hFontAlgo = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
    SelectObject(hdc, hFontAlgo);
    SetTextColor(hdc, current_algorithm == ALGO_LEAST_LOADED ? RGB(52, 152, 219) : RGB(155, 89, 182));
    TextOutA(hdc, 340, 32, current_algorithm == ALGO_LEAST_LOADED ? "Least-Loaded" : "Round-Robin", 12);
    DeleteObject(hFontAlgo);

    // Summary Metric Cards
    DrawRoundedCard(hdc, 480, 12, 620, 54, RGB(28, 36, 48), RGB(45, 58, 75));
    DrawRoundedCard(hdc, 630, 12, 770, 54, RGB(28, 36, 48), RGB(45, 58, 75));

    SelectObject(hdc, hFontSub);
    SetTextColor(hdc, RGB(140, 155, 175));
    TextOutA(hdc, 492, 16, "COMPLETED", 9);
    TextOutA(hdc, 642, 16, "SYSTEM LOAD", 11);

    HFONT hFontStat = CreateFont(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
    SelectObject(hdc, hFontStat);
    SetTextColor(hdc, RGB(82, 196, 26));

    pthread_mutex_lock(&stats_lock);
    char statBuf1[32], statBuf2[32];
    sprintf(statBuf1, "%d Tasks", completed_tasks_count);
    pthread_mutex_unlock(&stats_lock);
    sprintf(statBuf2, "%ds Work", totalSystemLoad);

    TextOutA(hdc, 492, 32, statBuf1, strlen(statBuf1));
    TextOutA(hdc, 642, 32, statBuf2, strlen(statBuf2));
    DeleteObject(hFontStat);

    // Render Cores
    int startY = 65;
    for (int i = 0; i < NUM_CORES; i++) {
        pthread_mutex_lock(&cores[i].lock);
        int active = cores[i].active_tasks;
        int load = cores[i].total_remaining_load;

        DrawRoundedCard(hdc, 30, startY, 770, startY + 105, RGB(24, 31, 42), RGB(40, 52, 68));

        HFONT hFontCore = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
        SelectObject(hdc, hFontCore);
        SetTextColor(hdc, RGB(230, 238, 248));

        char coreTitle[32];
        sprintf(coreTitle, "CORE %d", i);
        TextOutA(hdc, 50, startY + 10, coreTitle, strlen(coreTitle));
        DeleteObject(hFontCore);

        SelectObject(hdc, hFontSub);
        SetTextColor(hdc, RGB(140, 155, 175));
        
        int loadPercent = (load * 5 > 100) ? 100 : load * 5;
        char metaInfo[128];
        sprintf(metaInfo, "Queue: %d Tasks   |   Load: %d%%   |   Est. Clearance: %ds", active, loadPercent, load);
        TextOutA(hdc, 140, startY + 12, metaInfo, strlen(metaInfo));

        if (cores[i].head != NULL) {
            DrawRoundedCard(hdc, 640, startY + 8, 750, startY + 30, RGB(15, 60, 40), RGB(20, 100, 60));
            SetTextColor(hdc, RGB(82, 225, 120));
            TextOutA(hdc, 660, startY + 11, "BUSY", 4);

            // Display Queued Tasks Line
            char activeTaskBuf[128];
            sprintf(activeTaskBuf, "Executing Task #%d [%ds/%ds]", 
                    cores[i].head->id, cores[i].head->remaining_time, cores[i].head->burst_time);
            SetTextColor(hdc, RGB(110, 190, 255));
            TextOutA(hdc, 50, startY + 36, activeTaskBuf, strlen(activeTaskBuf));

            // Render Queue Pipeline
            Task* qItem = cores[i].head->next;
            int qPos = 280;
            SetTextColor(hdc, RGB(160, 175, 195));
            TextOutA(hdc, qPos, startY + 36, "Waiting Queue:", 14);
            qPos += 105;

            while (qItem != NULL && qPos < 620) {
                char qBuf[32];
                sprintf(qBuf, "[T#%d:%ds]", qItem->id, qItem->remaining_time);
                DrawRoundedCard(hdc, qPos, startY + 34, qPos + 65, startY + 52, RGB(35, 45, 60), RGB(60, 75, 95));
                SetTextColor(hdc, RGB(200, 215, 230));
                TextOutA(hdc, qPos + 6, startY + 36, qBuf, strlen(qBuf));
                qPos += 72;
                qItem = qItem->next;
            }

        } else {
            DrawRoundedCard(hdc, 640, startY + 8, 750, startY + 30, RGB(40, 45, 55), RGB(60, 65, 75));
            SetTextColor(hdc, RGB(160, 170, 185));
            TextOutA(hdc, 663, startY + 11, "IDLE", 4);

            SetTextColor(hdc, RGB(100, 115, 130));
            TextOutA(hdc, 50, startY + 36, "Executing: Core IDLE (Waiting for workload allocation...)", 58);
        }

        DrawRoundedCard(hdc, 50, startY + 62, 750, startY + 80, RGB(15, 20, 28), RGB(35, 45, 58));

        COLORREF fillGradient;
        if (load == 0) fillGradient = RGB(45, 55, 70);
        else if (load < 8) fillGradient = RGB(46, 204, 113);
        else if (load < 16) fillGradient = RGB(241, 196, 15);
        else fillGradient = RGB(235, 77, 75);

        int maxPixelWidth = 698;
        int filledWidth = (load * 35 > maxPixelWidth) ? maxPixelWidth : load * 35;

        if (filledWidth > 0) {
            DrawRoundedCard(hdc, 51, startY + 63, 51 + filledWidth, startY + 79, fillGradient, fillGradient);
        }

        pthread_mutex_unlock(&cores[i].lock);
        startY += 112;
    }

    // Allocation Decision Log Console (Simulation Visualizer Box)
    DrawRoundedCard(hdc, 30, 520, 770, 605, RGB(12, 16, 22), RGB(35, 45, 60));
    SetTextColor(hdc, RGB(52, 152, 219));
    TextOutA(hdc, 40, 524, "BALANCER DECISION LOG (Live Simulation Telemetry):", 51);

    pthread_mutex_lock(&log_lock);
    int logY = 542;
    for (int l = 0; l < log_count; l++) {
        SetTextColor(hdc, (strstr(log_buffer[l], "[COMPLETE]") != NULL) ? RGB(82, 196, 26) : RGB(170, 185, 205));
        TextOutA(hdc, 40, logY, log_buffer[l], strlen(log_buffer[l]));
        logY += 14;
    }
    pthread_mutex_unlock(&log_lock);

    // Control Container
    DrawRoundedCard(hdc, 30, 612, 770, 660, RGB(20, 26, 36), RGB(40, 52, 68));
    DeleteObject(hFontSub);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            hBtnLight = CreateWindowA("BUTTON", "+ Light (2s)", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                      45, 618, 130, 35, hWnd, (HMENU)ID_BTN_ADD_LIGHT, NULL, NULL);

            hBtnHeavy = CreateWindowA("BUTTON", "+ Heavy (7s)", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                      190, 618, 130, 35, hWnd, (HMENU)ID_BTN_ADD_HEAVY, NULL, NULL);

            hBtnAlgo = CreateWindowA("BUTTON", "Switch Algo", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                     335, 618, 140, 35, hWnd, (HMENU)ID_BTN_TOGGLE_ALGO, NULL, NULL);

            hBtnToggle = CreateWindowA("BUTTON", "Pause Generator", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                                       490, 618, 160, 35, hWnd, (HMENU)ID_BTN_TOGGLE_GEN, NULL, NULL);
            break;
        }
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
            HDC hdc = pdis->hDC;

            char btnText[64];
            GetWindowTextA(pdis->hwndItem, btnText, sizeof(btnText));

            COLORREF bg = (pdis->itemState & ODS_SELECTED) ? RGB(45, 60, 80) : RGB(32, 42, 56);
            COLORREF border = RGB(55, 70, 92);
            
            DrawRoundedCard(hdc, pdis->rcItem.left, pdis->rcItem.top, pdis->rcItem.right, pdis->rcItem.bottom, bg, border);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(220, 230, 242));
            HFONT hFontBtn = CreateFont(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, TEXT("Segoe UI"));
            HGDIOBJ hOldFont = SelectObject(hdc, hFontBtn);

            DrawTextA(hdc, btnText, -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFontBtn);
            return TRUE;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_ADD_LIGHT) {
                add_user_task(2);
            } else if (wmId == ID_BTN_ADD_HEAVY) {
                add_user_task(7);
            } else if (wmId == ID_BTN_TOGGLE_ALGO) {
                current_algorithm = (current_algorithm == ALGO_LEAST_LOADED) ? ALGO_ROUND_ROBIN : ALGO_LEAST_LOADED;
            } else if (wmId == ID_BTN_TOGGLE_GEN) {
                auto_generator_active = !auto_generator_active;
                SetWindowTextA(hBtnToggle, auto_generator_active ? "Pause Generator" : "Resume Generator");
            }
            break;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, 810, 680);
            HANDLE hOld = SelectObject(hdcMem, hbmMem);

            HBRUSH hBg = CreateSolidBrush(RGB(14, 18, 24));
            RECT rectWin = {0, 0, 810, 680};
            FillRect(hdcMem, &rectWin, hBg);
            DeleteObject(hBg);

            DrawModernGUI(hdcMem);

            BitBlt(hdc, 0, 0, 810, 680, hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, hOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hWnd, &ps);
            break;
        }
        case WM_TIMER:
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        case WM_DESTROY:
            running = false;
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int main() {
    srand(time(NULL));

    add_log_entry("[SYSTEM] Dynamic CPU Load Balancer Engine Initialized.");

    for (int i = 0; i < NUM_CORES; i++) {
        cores[i].id = i;
        cores[i].head = cores[i].tail = NULL;
        cores[i].active_tasks = 0;
        cores[i].total_remaining_load = 0;
        pthread_mutex_init(&cores[i].lock, NULL);
        pthread_create(&cores[i].thread_id, NULL, cpu_worker, &cores[i]);
    }

    pthread_t gen_thread;
    pthread_create(&gen_thread, NULL, task_generator, NULL);

    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "AdvancedCPULoadBalancer";

    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA("AdvancedCPULoadBalancer", "CPU Load Balancer Simulation Telemetry (Advanced C System Project)",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, 816, 710,
                              NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    SetTimer(hWnd, 1, 50, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    for (int i = 0; i < NUM_CORES; i++) {
        pthread_join(cores[i].thread_id, NULL);
        pthread_mutex_destroy(&cores[i].lock);
    }

    return 0;
}