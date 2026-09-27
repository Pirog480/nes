# nes

* `uusa.html` — исходный одностраничный UI «Cheat Menu UI» (HTML/CSS/JS, тёмная тема, вкладки Aim / Visuals / Heroes / Misc / Settings).
* [`nexus_imgui/`](nexus_imgui/) — **порт `uusa.html` на Dear ImGui (C++)**:
  * `src/nexus_menu.*`, `src/nexus_widgets.*` — перенос всего UI (темы, слайдеры, тумблеры, свотчи, keybind-захват, hero-панель, FOV-круги, тосты, drag, анимации) — зависит только от `<imgui.h>`;
  * `src/main.cpp` — демо под GLFW + OpenGL3;
  * `tools/` — headless-рендер (софтверный растеризатор ImDrawData → PPM/PNG), конвертер и юнит-тесты (`nexus_logic_test` → `ALL PASS`);
  * `shots/` — готовые скриншоты всех вкладок (тёмная/светлая тема, hero-панель, FOV, тост) + `misc.png`, `esp.png`, `esp_demo.png`.

  Сборка: `cmake -B build && cmake --build build -j` (см. [`nexus_imgui/README.md`](nexus_imgui/README.md)).

* Игровая логика Deadlock (Citadel, Source 2, DX11) — [`nexus_imgui/`](nexus_imgui/):
  * `src/game_offsets.h` — оффсеты из schema-дампов (`Client.dll.txt`, `sdk/`, `gen/`);
  * `src/pattern.*` — IDA-сканер сигнатур (`Find`/`FindRip`) для поиска entity list;
  * `src/game.*` — снапшот игроков/камеры и чистая математика: World→Screen, `CalcAngles`, нормировка/сглаживание углов, валидация скелета; `src/game_stub.cpp` — бэкенд «игры нет»;
  * `src/esp.*` — ESP: боксы, хелсбары, имена с дистанцией, скелеты (рисуется в background draw list **после** `nexus::DrawMenu()`);
  * `src/aim.*` — векторный аим (режим Normal): конус, HS Limit, предикт движения, приоритеты целей, сглаживание, запись углов;
  * `dll/` — инжектируемая `nexus.dll` (WIN32/MSVC): `DllMain` → ожидание `client.dll` → патч vtable `IDXGISwapChain` (Present = 8, ResizeBuffers = 13), ImGui поверх DX11, настоящий бэкенд `game_windows.cpp` (Capture / запись углов / FOV changer / auto active reload), лог в `%TEMP%\nexus_dll.log`.

  Инструкции по сборке DLL, инжекту, обновлению после патча игры и headless-скриншотам (`--esp-demo`) — в [`nexus_imgui/README.md`](nexus_imgui/README.md).

---

> **Дисклеймер.** Проект educational-only: только для обучения (чтение структур
> Source 2, математика проекции, ImGui-рендер, устройство DX11-хуков). Обхода
> античита нет и не подразумевается; в онлайне не использовать.
