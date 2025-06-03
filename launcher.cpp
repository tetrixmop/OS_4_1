// launcher.cpp
#include <windows.h>
#include <iostream>
#include <vector>
#include <string>

#define WRITERS_TOTAL 10
#define READERS_TOTAL 10

int main()
{
    // В буфере храним все дескрипторы hProcess для ожидания
    HANDLE handles[WRITERS_TOTAL + READERS_TOTAL] = { 0 };
    int idx = 0;

    // 1) Запускаем несколько writer-процессов
    for (int i = 0; i < WRITERS_TOTAL; ++i) {
        STARTUPINFOW sinf = { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION pinf;
        std::wstring cmd = L"Writer.exe";

        BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &sinf, &pinf);
        if (ok) {
            std::wcout << L"[Launcher] Writer #" << (i + 1) << L" launched.\n";
            handles[idx++] = pinf.hProcess;
            CloseHandle(pinf.hThread);
        }
        else {
            std::wcerr << L"[Launcher] Unable to start Writer #" << (i + 1)
                << L". Error = " << GetLastError() << L"\n";
        }
    }

    // 2) Запускаем несколько reader-процессов
    for (int i = 0; i < READERS_TOTAL; ++i) {
        STARTUPINFOW sinf = { sizeof(STARTUPINFOW) };
        PROCESS_INFORMATION pinf;
        std::wstring cmd = L"Reader.exe";

        BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &sinf, &pinf);
        if (ok) {
            std::wcout << L"[Launcher] Reader #" << (i + 1) << L" launched.\n";
            handles[idx++] = pinf.hProcess;
            CloseHandle(pinf.hThread);
        }
        else {
            std::wcerr << L"[Launcher] Unable to start Reader #" << (i + 1)
                << L". Error = " << GetLastError() << L"\n";
        }
    }

    // 3) Ждём, пока все дочерние процессы закончатся
    std::wcout << L"[Launcher] Waiting for all processes...\n";
    if (idx > 0) {
        WaitForMultipleObjects(idx, handles, TRUE, INFINITE);
    }

    // 4) Закрываем дескрипторы
    for (int i = 0; i < idx; ++i) {
        CloseHandle(handles[i]);
    }

    std::wcout << L"[Launcher] Process finished. Press ENTER to exit.\n";
    std::wcin.get();
    return 0;
}
