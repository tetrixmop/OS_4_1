// reader.cpp
#include <windows.h>
#include <iostream>
#include <fstream>
#include <random>
#include <string>
#include <chrono>
#include <process.h>
#include <mmsystem.h>
#include <sstream>
#pragma comment(lib, "winmm.lib")

#define BUFF_PAGES 20
#define BUFF_BYTES 4096

enum ChunkState { C_EMPTY = 0, C_WRITTEN = 1, C_READ = 2 };

struct SharedBuffer {
    ChunkState states[BUFF_PAGES];
    char payload[BUFF_PAGES][BUFF_BYTES];
};

static const wchar_t* MUTEX_NAME = L"Global_SharedBuf_Mutex";
static const wchar_t* MAPPING_NAME = L"Global_SharedBuf_Mapping";
static const wchar_t* SEM_NAME_BASE = L"Global_PageSem_";

HANDLE g_hFileMapping = nullptr;
SharedBuffer* g_bufView = nullptr;
HANDLE g_mutexHandle = nullptr;
HANDLE g_pageSems[BUFF_PAGES] = { 0 };
std::ofstream g_logStream;

static DWORD getMilliTime() {
    return timeGetTime();
}

// Завершиться с ошибкой (пишем в stderr и exit)
static void failExit(const char* msg) {
    std::cerr << "[Reader] Error: " << msg << ", code GetLastError() = " << GetLastError() << "\n";
    ExitProcess(1);
}


std::wstring to_wstring_alt(int value) {
    std::wstringstream ss;
    ss << value;
    return ss.str();
}

// Открываем мьютекс, семафоры, карту памяти, открываем лог
void initAll()
{
    srand(static_cast<unsigned int>(time(nullptr)));

    // 1) Открываем мьютекс (он должен быть уже создан писателем)
    g_mutexHandle = OpenMutexW(
        MUTEX_ALL_ACCESS,
        FALSE,
        MUTEX_NAME
    );
    if (g_mutexHandle == nullptr) {
        failExit("OpenMutexW failed");
    }

    // 2) Открываем семафоры для каждой страницы
    for (int i = 0; i < BUFF_PAGES; ++i) {
        std::wstring semFullName = SEM_NAME_BASE + to_wstring_alt(i);
        g_pageSems[i] = OpenSemaphoreW(
            SEMAPHORE_ALL_ACCESS,
            FALSE,
            semFullName.c_str()
        );
        if (g_pageSems[i] == nullptr) {
            failExit("OpenSemaphoreW failed");
        }
    }

    // 3) Открываем file-mapping
    g_hFileMapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        MAPPING_NAME
    );
    if (g_hFileMapping == nullptr) {
        failExit("OpenFileMappingW failed");
    }

    g_bufView = reinterpret_cast<SharedBuffer*>(
        MapViewOfFile(
            g_hFileMapping,
            FILE_MAP_ALL_ACCESS,
            0, 0,
            sizeof(SharedBuffer)
        )
        );
    if (g_bufView == nullptr) {
        CloseHandle(g_hFileMapping);
        failExit("MapViewOfFile failed");
    }

    // 4) Открываем (или создаём, если нет) файл лога: reader_log_<PID>.txt
    DWORD pid = GetCurrentProcessId();
    std::string logName = "reader_log_" + std::to_string(pid) + ".txt";
    g_logStream.open(logName, std::ios::out | std::ios::app);
    if (!g_logStream.is_open()) {
        std::cerr << "[Reader] Unable to open file " << logName << "\n";
        // но не фейлим — всё равно попробуем читать
    }
}

// Освобождаем ресурсы
void cleanupAll()
{
    if (g_bufView) {
        UnmapViewOfFile(g_bufView);
    }
    if (g_hFileMapping) {
        CloseHandle(g_hFileMapping);
    }
    for (int i = 0; i < BUFF_PAGES; ++i) {
        if (g_pageSems[i]) {
            CloseHandle(g_pageSems[i]);
        }
    }
    if (g_mutexHandle) {
        CloseHandle(g_mutexHandle);
    }
    if (g_logStream.is_open()) {
        g_logStream.close();
    }
}

// Ждём, когда освободится один из семафоров. Если получено OK - индекс семафора
int waitForChunk()
{
    DWORD res = WaitForMultipleObjects(
        BUFF_PAGES,
        g_pageSems,
        FALSE,
        1500  // таймаут 1.5с
    );

    if (res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + BUFF_PAGES) {
        return static_cast<int>(res - WAIT_OBJECT_0);
    }
    return -1;
}

void performRead(int chunkIdx)
{
    // a) Захватываем мьютекс и проверяем, что состояние WRITTEN
    WaitForSingleObject(g_mutexHandle, INFINITE);
    if (g_bufView->states[chunkIdx] == C_WRITTEN) {
        g_bufView->states[chunkIdx] = C_READ;
    }
    ReleaseMutex(g_mutexHandle);

    // b) Логируем начало чтения
    if (g_logStream.is_open()) {
        g_logStream << GetCurrentProcessId() << ":"
            << getMilliTime()
            << ": Start_Read_Chunk_" << chunkIdx << "\n";
    }

    // c) Читаем всю страницу побайтово (симуляция cpu-загрузки)
    volatile char tmp;
    for (int i = 0; i < BUFF_BYTES; ++i) {
        tmp = g_bufView->payload[chunkIdx][i];
    }

    // d) Имитируем длительность чтения 
    int delay = 500 + rand() % 1001;
    Sleep(delay);

    // e) Логируем «конец чтения»
    if (g_logStream.is_open()) {
        g_logStream << GetCurrentProcessId() << ":"
            << getMilliTime()
            << ": End_Read_Chunk_" << chunkIdx << "\n";
    }

    // f) Короткий sleep, чтобы разнести точки на графике
    Sleep(5);
}

int main()
{
    initAll();

    for (int i = 0; i < BUFF_PAGES; ++i) {
        if (g_logStream.is_open()) {
            g_logStream << GetCurrentProcessId() << ":"
                << getMilliTime()
                << ": Waiting_For_Chunk\n";
        }

        int pageIndex = waitForChunk();
        if (pageIndex < 0) {
            Sleep(100);
            continue;
        }

        performRead(pageIndex);
    }

    cleanupAll();
    return 0;
}
