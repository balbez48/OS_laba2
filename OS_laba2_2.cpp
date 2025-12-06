#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <windows.h>
#include <string>
#include <sstream>

void child_process(int n, int start_row, int end_row, int process_id) {
    // 1. Открываем общую память
    HANDLE shared_memory = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"MyMatrixMemory");
    if (!shared_memory) {
        return;
    }

    // 2. Подключаемся к этой памяти
    int* memory = (int*)MapViewOfFile(shared_memory, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!memory) {
        CloseHandle(shared_memory);
        return;
    }

    // 3. Читаем данные из общей памяти
    int* A = memory + 1;
    int* B = A + n * n;
    int* C = B + n * n;

    // 4. Вычисляем СВОЮ часть
    for (int i = start_row; i < end_row; i++) {
        for (int j = 0; j < n; j++) {
            int sum = 0;
            for (int k = 0; k < n; k++) {
                sum += A[i * n + k] * B[k * n + j];
            }
            C[i * n + j] = sum;
        }
    }

    // 5. Отключаемся от общей памяти
    UnmapViewOfFile(memory);
    CloseHandle(shared_memory);
}

void parent_process() {
    int n = 500;
    int num_processes = 4;
    srand(time(0));

    std::vector<int> A(n * n);
    std::vector<int> B(n * n);

    // Заполняем матрицы
    for (int i = 0; i < n * n; i++) {
        A[i] = rand() % 10;
        B[i] = rand() % 10;
    }

    // Создаем общую память
    size_t memory_size = sizeof(int) * (1 + 3 * n * n);
    HANDLE shared_memory = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0, static_cast<DWORD>(memory_size),
        L"MyMatrixMemory"
    );

    if (!shared_memory) {
        std::cerr << "Не могу создать общую память" << std::endl;
        return;
    }

    int* memory = (int*)MapViewOfFile(shared_memory, FILE_MAP_ALL_ACCESS, 0, 0, memory_size);
    if (!memory) {
        std::cerr << "Не могу подключиться к общей памяти" << std::endl;
        CloseHandle(shared_memory);
        return;
    }

    // Записываем данные
    memory[0] = n;
    int* shared_A = memory + 1;
    int* shared_B = shared_A + n * n;
    int* shared_C = shared_B + n * n;

    for (int i = 0; i < n * n; i++) {
        shared_A[i] = A[i];
        shared_B[i] = B[i];
        shared_C[i] = 0;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // СОЗДАЕМ ПРОЦЕССЫ
    std::vector<HANDLE> process_handles(num_processes);
    int rows_per_process = n / num_processes;
    int current_row = 0;

    // Получаем путь к текущему исполняемому файлу (UNICODE версия)
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    for (int p = 0; p < num_processes; p++) {
        int start_row = current_row;
        int end_row = start_row + rows_per_process;

        if (p < n % num_processes) {
            end_row++;
        }
        current_row = end_row;

        // Формируем командную строку (UNICODE)
        std::wstringstream cmd;
        cmd << L"\"" << exe_path << L"\" " << n << L" " << start_row << L" " << end_row << L" " << p;
        std::wstring cmd_str = cmd.str();

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        // Используем wchar_t* для командной строки
        wchar_t* cmd_line = new wchar_t[cmd_str.size() + 1];
        wcscpy_s(cmd_line, cmd_str.size() + 1, cmd_str.c_str());

        if (CreateProcessW(
            NULL,           // Имя исполняемого файла
            cmd_line,       // Командная строка (UNICODE)
            NULL, NULL,     // Атрибуты безопасности
            FALSE,          // Не наследовать дескрипторы
            0,              // Флаги создания
            NULL, NULL,     // Окружение и текущий каталог
            &si,            // STARTUPINFO
            &pi             // PROCESS_INFORMATION
        )) {
            process_handles[p] = pi.hProcess;
            CloseHandle(pi.hThread);
        }
        else {
            std::cerr << "Ошибка создания процесса " << p
                << ", код: " << GetLastError() << std::endl;
            process_handles[p] = NULL;
        }

        delete[] cmd_line;
    }

    // ЖДЕМ завершения всех процессов
    WaitForMultipleObjects(num_processes, process_handles.data(), TRUE, INFINITE);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Проверка результата
    int test_sum = 0;
    for (int k = 0; k < n; k++) {
        test_sum += A[k] * B[k * n];
    }

    std::cout << "Матрица: " << n << "x" << n << std::endl;
    std::cout << "Процессов: " << num_processes << std::endl;
    std::cout << "Время: " << duration.count() << " ms" << std::endl;
    std::cout << "C[0][0] = " << shared_C[0] << " (должно быть: " << test_sum << ")" << std::endl;

    // Очистка
    for (int p = 0; p < num_processes; p++) {
        if (process_handles[p]) {
            CloseHandle(process_handles[p]);
        }
    }

    UnmapViewOfFile(memory);
    CloseHandle(shared_memory);
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "RUS");

    if (argc > 1) {
        // Дочерний процесс
        int n = atoi(argv[1]);
        int start_row = atoi(argv[2]);
        int end_row = atoi(argv[3]);
        int process_id = atoi(argv[4]);

        child_process(n, start_row, end_row, process_id);
    }
    else {
        // Родительский процесс
        parent_process();
    }

    return 0;
}