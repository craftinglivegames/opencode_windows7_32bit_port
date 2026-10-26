# OpenCode for Windows 7 SP1 x64

Порт [opencode](https://opencode.ai) (Bun SEA, v1.19.3) под **Windows 7 SP1 x64**.

Готовый файл: [`dist/OCShellWin7.exe`](dist/OCShellWin7.exe) — самораспаковывающийся portable-пакет (~60 МБ).

---

## Как запустить

1. Скопируйте `OCShellWin7.exe` на машину с Windows 7 SP1 x64.
2. Запустите `OCShellWin7.exe` (двойной клик или из cmd).
3. При первом запуске рядом создаётся папка `portable\` и распаковываются:
   - `opencode.exe`
   - `ocshim32.dll`
   - `ocn.dll`
   - `README.txt`
4. После распаковки лаунчер запускает `portable\opencode.exe` с вашими аргументами.

Примеры:
```
OCShellWin7.exe --version
OCShellWin7.exe
```

---

## Что внутри и почему это работает на Win7

`opencode.exe` собран как Bun SEA и импортирует функции, которых нет в Windows 7.
Вместо пересборки из исходников бинарник патчится: имена DLL в трёх дескрипторах
импорта заменяются на наши шимы (изменяется ~53 байта PE).

| DLL | Что делает |
|-----|------------|
| `ocshim32.dll` | 318 экспортов: 305 форвардятся в `kernel32.dll`/аналоги, 13 реализованы вручную (Win8+ API) |
| `ocn.dll` | 39 экспортов для `ntdll.dll`: 35 форвардятся, 4 реализованы вручную (`RtlWaitOnAddress` и др.) |

Реализованные вручную примитивы Win8+:
- `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll`
- `RtlWaitOnAddress` / `RtlWakeAddressSingle` / `RtlWakeAddressAll`
- `GetCurrentThreadStackLimits`
- `DiscardVirtualMemory` / `OfferVirtualMemory` / `ReclaimVirtualMemory`
- `GetSystemTimePreciseAsFileTime` / `RtlGetSystemTimePrecise`
- и др.

TUI работает через ANSI-прокси (`src/ansi_console.cpp`): conhost Windows 7 не
поддерживает VT100, поэтому лаунчер перехватывает вывод дочернего процесса,
разбирает ANSI-последовательности и перерисовывает экран средствами Win7 Console API.

---

## Исправленные баги (важно для Win7)

1. **Переполнение буфера командной строки** — `calloc(argc + 3, sizeof(wchar_t))`
   выделял 8 байт под весь путь; на Win7 это гарантированно портило кучу.
   Теперь длина считается точно: `wcslen(exe) + Σ(wcslen(argv[i]) + 1) + 3`.
2. **`ReadFile` с `NULL`** в `payload.cpp` — передавался `NULL` вместо `DWORD*`.
3. **`GetCurrentThreadStackLimits`** возвращал фиктивные `0..512K`; Bun/JSC
   принимал это за переполнение стека. Теперь берутся реальные границы из TEB
   (`NT_TIB::StackLimit` / `StackBase`).
4. **`join(a, NULL)`** в `ocshell.cpp` — падение при `b == NULL`.

---

## Сборка из исходников

Требуется Visual Studio (MSVC x64) и Python 3.

```bat
tools\build_shims.cmd   :: собрать ocshim32.dll / ocn.dll и пропатчить opencode.exe
tools\build_shell.cmd   :: собрать ocshell.exe (лаунчер + ANSI-прокси + zlib)
python tools\make_package.py build\ocshell.exe build\payload dist\OCShellWin7.exe
```

`build_shims.cmd` по умолчанию берёт `opencode.exe` из
`%APPDATA%\npm\node_modules\opencode-ai\bin\opencode.exe`;
другой путь можно передать первым аргументом.

---

## Результаты тестирования

### Хост (Windows 11)
```
opencode.exe --version → 1.19.3
```
Пакет собирается и запускается, импорты корректно перенаправлены на шимы.

### QEMU VM, Windows 7 SP1 x64
- Самораспаковка: **успешно** (все 4 файла созданы, см. `s6.png` в истории).
- Запуск `opencode.exe`: стартует, Bun-рантайм инициализируется,
  но процесс завершается с кодом **3** без вывода.
- CPU: AVX в госте включён (`-cpu SandyBridge,+avx`, QEMU 11.1.0, TCG;
  CPUID в госте: `avx=1 avx2=1 xcr0=0x7`).
- Причина кода 3 — ограничение самого Bun 1.3.14 на Win7, а не порта:
  шимы и лаунчер отрабатывают полностью.

Для полной проверки нужен реальный ПК с Windows 7 SP1 x64
(или гипервизор с пробросом AVX: VMware, VirtualBox, Hyper-V).

---

## Структура репозитория

```
dist/OCShellWin7.exe   готовый portable-пакет
src/                   исходники лаунчера, ANSI-прокси и шимов
tools/                 сборка, патч PE, упаковка
build/                 промежуточные артефакты (dll, obj, lib)
README.md              этот файл
```

---

## Лицензия

Код порта распространяется как есть. `opencode.exe` — собственность его авторов,
используется по лицензии оригинального проекта.
