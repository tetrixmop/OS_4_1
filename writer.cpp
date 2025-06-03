// writer.cpp
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

// Возможные состояния каждой страницы
enum ChunkState { C_EMPTY = 0, C_WRITTEN = 1, C_READ = 2 };

// Структура, хранящая массив состояний + данные (в виде байтов)
struct SharedBuffer {
    ChunkState states[BUFF_PAGES];
    char payload[BUFF_PAGES][BUFF_BYTES];
};

// Имена синхропримитивов и mapped-файла
static const wchar_t* MUTEX_NAME = L"Global_SharedBuf_Mutex";
static const wchar_t* MAPPING_NAME = L"Global_SharedBuf_Mapping";
static const wchar_t* SEM_NAME_BASE = L"Global_PageSem_"; // будем к нему добавлять номер

// Глобальные переменные для упрощения
HANDLE g_hFileMapping = nullptr;
SharedBuffer* g_bufView = nullptr;
HANDLE g_mutexHandle = nullptr;
HANDLE g_pageSems[BUFF_PAGES] = { 0 };
std::ofstream g_logStream;


// Получить «современный» миллисекундный таймстамп через timeGetTime()
static DWORD getMilliTime() {
    return timeGetTime();
}

// Вывод сообщения об ошибке и завершение процесса
static void failExit(const char* msg) {
    std::cerr << "[Writer] Error: " << msg << ", code GetLastError() = " << GetLastError() << "\n";
    ExitProcess(1);
}

// Починка ошибки с to_wstring
std::wstring to_wstring_alt(int value) {
    std::wstringstream ss;
    ss << value;
    return ss.str();
}

// Инициализация общей памяти, мьютекса, семафоров, файла лога
void initAll()
{
    // 1) Инициализируем srand для задержек
    srand(static_cast<unsigned int>(time(nullptr)));

    // 2) Создаём/открываем глобальный мьютекс (чтобы синхронизация была на уровне системы)
    g_mutexHandle = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (g_mutexHandle == nullptr) {
        failExit("CreateMutexW failed");
    }

    // 3) Создаём семафоры для каждой страницы
    for (int i = 0; i < BUFF_PAGES; ++i) {
        std::wstring semFullName = SEM_NAME_BASE + to_wstring_alt(i);
        g_pageSems[i] = CreateSemaphoreW(
            nullptr,
            0,      // начальный счётчик = 0 (пока нет данных)
            1,      // макс = 1 (после записи один читатель может получить)
            semFullName.c_str()
        );
        if (g_pageSems[i] == nullptr) {
            failExit("CreateSemaphoreW failed");
        }
    }

    // 4) Создаём проецируемый (file-mapped) файл нужного размера
    g_hFileMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,  // резервируем в памяти, не под файл на диске
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(SharedBuffer),
        MAPPING_NAME
    );
    if (g_hFileMapping == nullptr) {
        failExit("CreateFileMappingW failed");
    }

    // Привязываем указатель к отражённой памяти
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

    // 5) Блокируем все страницы в RAM, чтобы они не сбрасывались на диск
    if (!VirtualLock(g_bufView, sizeof(SharedBuffer))) {
        std::cerr << "[Writer] VirtualLock returned error: " << GetLastError() << "\n";
        // Но не фэйлим — продолжаем, хотя без блокировки
    }

    // 6) Открываем (создаём) файл для логирования: writer_log_<PID>.txt
    DWORD pid = GetCurrentProcessId();
    std::string logName = "writer_log_" + std::to_string(pid) + ".txt";
    g_logStream.open(logName, std::ios::out | std::ios::app);
    if (!g_logStream.is_open()) {
        std::cerr << "[Writer] Unable to open log file " << logName << "\n";
        // не прекращаем полностью работу, но без лога
    }
}

// Освобождаем всё: VirtualUnlock, UnmapView, CloseHandle для семафоров и мьютекса, закрываем лог
void cleanupAll()
{
    if (g_bufView) {
        VirtualUnlock(g_bufView, sizeof(SharedBuffer));
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

// Ищем свободную или «прочитанную» страницу. Если найдена, сразу помечаем её как C_WRITTEN.
int locateFreeChunk()
{
    // ждем мьютекса до бесконечности
    WaitForSingleObject(g_mutexHandle, INFINITE);

    int found = -1;
    for (int i = 0; i < BUFF_PAGES; ++i) {
        ChunkState st = g_bufView->states[i];
        if (st == C_EMPTY || st == C_READ) {
            found = i;
            g_bufView->states[i] = C_WRITTEN; // сразу отмечаем, что я буду здесь писать
            break;
        }
    }

    ReleaseMutex(g_mutexHandle);
    // если всё занято, found = -1
    return found;
}

// Процесс «записи» на страницу с индексом idx.
// Копирует в неё случайную последовательность байтов, пишет логи и отдаёт семафор, чтобы читатель прочитал.
void performWrite(int idx)
{
    // a) генерируем произвольные данные
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < BUFF_BYTES; ++i) {
        g_bufView->payload[idx][i] = static_cast<char>(dist(rng));
    }

    // b) логируем «начало записи» с таймстампом
    if (g_logStream.is_open()) {
        g_logStream << GetCurrentProcessId() << ":"
            << getMilliTime()
            << ": Start_Write_Chunk_" << idx << "\n";
    }

    // c) эмулируем замедленное копирование (500–1500 мс)
    int delay = 500 + rand() % 1001; // от 500 до 1500
    Sleep(delay);

    // d) логируем «конец записи»
    if (g_logStream.is_open()) {
        g_logStream << GetCurrentProcessId() << ":"
            << getMilliTime()
            << ": End_Write_Chunk_" << idx << "\n";
    }

    // e) «открываем» семафор, чтобы один из читателей заблокился
    ReleaseSemaphore(g_pageSems[idx], 1, nullptr);

    // Небольшая пауза, чтобы на графике было видно
    Sleep(5);
}

int main()
{
    initAll();

    // Выполняем ровно BUFF_PAGES итераций: так же, как было 14 раз
    for (int i = 0; i < BUFF_PAGES; ++i) {
        // Логируем, что сейчас ожидаем страницу (аналог «Waiting for a page to write»)
        if (g_logStream.is_open()) {
            g_logStream << GetCurrentProcessId() << ":"
                << getMilliTime()
                << ": Waiting_For_Chunk\n";
        }
        // Поиск страницы
        int slot = locateFreeChunk();
        if (slot < 0) {
            // Если «None», спим немного и переходим к следующей итерации
            Sleep(100);
            continue;
        }

        performWrite(slot);
    }

    cleanupAll();
    return 0;
}
