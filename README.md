# TextMagic

[Русский](README.md) | [English](README.en.md)

TextMagic — приложение для Windows, которое обрабатывает текст в активном поле ввода по глобальным горячим клавишам. Возможности приложения расширяются с помощью файлов `.tmscript`, поэтому для добавления и изменения сценариев не требуется перекомпиляция.

## Сборка

Нужны Windows x64, Microsoft Build Tools 2026 с компонентами C++ и Windows SDK,
а также CMake 4.2 или новее для генератора Visual Studio 18 2026.
Среда Visual Studio IDE не требуется.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -T v145
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Исполняемый файл: `build\bin\Release\TextMagic.exe`. Сборка Release x64 проверена
с MSVC 19.51.36260, Windows SDK 10.0.26100.0 и CMake 4.4.4.
Если папка `build` ранее использовала другой генератор, добавьте `--fresh`
к команде конфигурации. Библиотеки MSVC подключаются динамически (`/MD` в Release).
Для запуска нужен актуальный [Microsoft Visual C++ Redistributable x64](https://aka.ms/vc14/vc_redist.x64.exe).
Если пакет отсутствует или устарел, установите или обновите его. Visual Studio для запуска не требуется.
Для выполнения сценариев используется встроенный в Windows Windows PowerShell.

## Встроенные сценарии

| Сценарий | Горячая клавиша | Назначение |
| --- | --- | --- |
| Layout Auto QWERTY | `Shift+Shift` | Исправляет текст, набранный в неверной русской или английской раскладке QWERTY, сохраняет регистр букв и переключает язык ввода на язык результата. |
| Next Keyboard Layout | `Shift` | Переключает активное приложение на следующую установленную раскладку клавиатуры. |
| Uppercase | `Ctrl+Alt+U` | Преобразует текст в верхний регистр. |
| Lowercase | `Ctrl+Alt+L` | Преобразует текст в нижний регистр. |

Горячие клавиши не зашиты в приложение: их можно изменить в соответствующих файлах `.tmscript`.

## Как выбирается текст

При запуске сценария TextMagic использует:

1. явно выделенный текст;
2. если выделения нет — последнее введённое слово или весь отслеживаемый текст, в зависимости от выбранного режима.

Без явного выделения TextMagic не считывает всё содержимое активного поля ввода.

## Сценарии `.tmscript`

Сценарии загружаются из папки `scripts`, расположенной рядом с `TextMagic.exe`. Каждый сценарий хранится в отдельном файле `.tmscript`, который содержит метаданные и тело PowerShell-скрипта.

Пример:

```ini
name=Uppercase
description=Converts selected or recently typed text to uppercase
hotkey=Ctrl+Alt+U
enabled=true
---
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$inputText = [Console]::In.ReadToEnd()
[Console]::Out.Write($inputText.ToUpperInvariant())
```

Поля манифеста:

- `name` — имя сценария в приложении;
- `description` — краткое описание;
- `hotkey` — горячая клавиша или сочетание клавиш;
- `enabled` — состояние сценария (`true` или `false`);
- строка `---` отделяет метаданные от тела PowerShell-скрипта.

## Протокол выполнения

1. TextMagic передаёт исходный текст сценарию через `stdin` в кодировке UTF-8.
2. Сценарий возвращает обработанный текст через `stdout` в кодировке UTF-8.
3. Успешно выполненный сценарий должен завершиться с кодом `0`.

## Лицензия

Проект распространяется по лицензии [MIT](LICENSE).
