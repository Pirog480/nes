# nexus_imgui — `uusa.html` → Dear ImGui (C++)

Порт одностраничного UI **NEXUS «Cheat Menu UI»** (`uusa.html`, HTML/CSS/JS)
на **Dear ImGui v1.91.9b**. Вся структура, вкладки, контролы и поведение
исходной страницы воспроизведены нативным C++ кодом.

## Что перенесено

| HTML / CSS / JS | ImGui-порт |
|---|---|
| Темы `dark` / `light` (CSS-переменные) | `ApplyStyle()` — палитры `kDark` / `kLight` |
| Акцентный цвет (свотчи) | `kAccentColors[]`, применяется к стилю и виджетам |
| Glassmorphism (blur, backdrop-filter) | Полупрозрачные фоны + рамки (blur недоступен в ImGui — эмуляция) |
| Анимированные фоновые «блобы» | Удалены: фон существовал только для превью стекла, в игре закрывал кадр |
| Шапка: brand, FPS, minimize, close, drag | `DrawHeader()` — перетаскивание шапки, пульсирующая точка, FPS реальный |
| Сайдбар 180px с индикатором вкладок | `DrawSidebar()` — SVG-иконки перерисованы примитивами, индикатор с анимацией |
| Карточки `.card` + `.card-title` | `BeginCard()/EndCard()` (child-окно со скруглением и hover-подсветкой) |
| Тумблеры `.toggle` | `Toggle()/ToggleRow()` — с анимацией ручки |
| Слайдеры + бейдж значения | `SliderRow()` — свой слайдер (трек, заливка, «кнопка», `%`/`°`/`m`/`ms`) |
| Segmented control (+ underline) | `Segmented()` — боксированный и подчёркивающий варианты |
| Чипы `.chip` (Aim Points) | `Chip()` |
| Кейбинд с захватом клавиши/мыши | `KeybindRow()` + `UpdateCapture()` (Escape → «–», MB1/MB2/MB3) |
| Свотчи цветов `.swatch` | `Swatches()` — с галочкой у активного |
| Коллапсирующиеся секции | `CollapseSliderRow()` (анимация высоты) |
| Hero-грид + панель героя (overlay) | `DrawHeroesTab()` + `DrawHeroOverlay()` — затемнение, тень, поиск |
| FOV-круги по центру экрана | `DrawFovCircles()` — обводка + свечение, цвет/прозрачность из настроек |
| Тосты уведомлений | `DrawToasts()` — foreground draw list, всплытие/затухание |
| Клавиша **Insert** (toggle меню) | Обработка в `DrawMenu()`, ключ можно переназначить |
| UI Scale / Menu Opacity | `ScaleAllSizes()` + `FontGlobalScale` + альфа всех цветов |
| Минимизация окна | Плавная анимация высоты 520→48 |

Все значения по умолчанию (слайдеры, тумблеры, цвета) совпадают с HTML.

## Сборка

Требования: CMake ≥ 3.16, компилятор с C++17, сеть (FetchContent качает ImGui
и GLFW при первом configure).

```bash
cd nexus_imgui
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/nexus_demo              # демо с окном GLFW (Insert — показать меню)
./build/nexus_headless --tab aim --out aim.ppm   # headless-скриншот без GPU
python3 tools/ppm2png.py aim.ppm aim.png
```

Опции CMake:

* `NEXUS_BUILD_DEMO=ON|OFF` — демо-окно (нужны X11/Wayland + OpenGL). На машине
  без X11/GL собирайте с `-DNEXUS_BUILD_DEMO=OFF`.
* `NEXUS_BUILD_TOOLS=ON|OFF` — headless-инструмент и юнит-тесты (собирается везде).
* `NEXUS_BUILD_DLL=ON|OFF` — инжектируемая `nexus.dll`; по умолчанию `ON` только
  на Windows (цель собирается под MSVC; см. «Сборка DLL и инжект»).
* `NEXUS_IMGUI_DIR=/path` — использовать локальный исходник Dear ImGui
  **v1.91.9b** (папка с `imgui.h`) вместо FetchContent. Полезно, когда GitHub
  недоступен: рабочий источник — sdist `imgui-bundle==1.6.3` с PyPI, внутри
  `external/imgui/imgui` — ровно 1.91.9b. Версию 1.92+ не использовать: она
  ломает используемый здесь API.
* `NEXUS_GLFW_DIR=/path` — локальный исходник GLFW (папка с `CMakeLists.txt`)
  вместо FetchContent (тот же sdist содержит `external/glfw/glfw`).

Цели:

* `nexus_ui` — виджеты + меню;
* `nexus_logic` — `game.cpp`, `pattern.cpp`, `esp.cpp`, `aim.cpp` (чистая
  математика и рендер ESP/айма, без ОС);
* `nexus_game_stub` — бэкенд «игры нет» для headless/демо/тестов;
* `nexus_headless`, `nexus_logic_test` — линкуют `nexus_logic` + `nexus_game_stub`;
* `nexus` — DLL (только WIN32/MSVC); `dll/game_windows.cpp` даёт настоящий
  игровой бэкенд вместо stub.

## Интеграция в свой ImGui-проект

1. Скопируйте `src/nexus_widgets.*` и `src/nexus_menu.*` в свой проект.
2. Добавьте их в сборку рядом с вашим ImGui (нужен только `<imgui.h>`).
3. В кадре вызывайте:

```cpp
#include "nexus_menu.h"
// ...
ImGui::NewFrame();
nexus::DrawMenu();   // и всё
ImGui::Render();
```

4. Очисточный цвет фона: `glClearColor` из `nexus::GetClearColor()`
   (фон меню не заливает экран; в DLL виден кадр игры).
5. Состояние всех настроек — `nexus::GetState()` (структура `nexus::State`).

## Устройство кода

```
src/nexus_widgets.h/.cpp — переиспользуемые контролы (toggle, slider, seg...)
src/nexus_menu.h/.cpp    — состояние, тема, вкладки, оверлеи, ввод
src/game_offsets.h       — оффсеты Deadlock (Citadel, Source 2) из schema-дампов
src/pattern.h/.cpp       — IDA-сканер сигнатур (Find / FindRip)
src/game.h/.cpp          — снапшот игры + чистая математика (W2S, углы, кости)
src/game_stub.cpp        — бэкенд «игры нет» (headless / демо / тесты)
src/esp.h/.cpp           — ESP (боксы, хелсбары, имена, скелеты)
src/aim.h/.cpp           — векторный аим (режим Normal), разбор кейбиндов
src/main.cpp             — демо-оболочка GLFW+OpenGL3 (не для интеграции)
dll/dll_log.h            — лог в OutputDebugString + %TEMP%\nexus_dll.log
dll/dllmain.cpp          — DllMain: поток, ожидание client.dll, Install()
dll/hooks.h/.cpp         — патч vtable IDXGISwapChain (Present/ResizeBuffers)
dll/game_windows.cpp     — настоящий бэкенд: entity list, Capture, запись углов
tools/headless_render.cpp— рендер ImDrawData софтвером → PPM (скриншоты)
tools/logic_test.cpp     — юнит-тесты математики/сканера/бэкенда («ALL PASS»)
tools/ppm2png.py         — PPM → PNG без зависимостей
```

## Известные отличия от оригинала

* **Blur/glass**: в ImGui нет `backdrop-filter` и SVG-фильтров; полноэкранные
  блобы удалены — в игре они закрывали кадр.
* **FPS-счётчик**: в HTML был случайный (135–154), здесь — реальный FPS.
* **Шрифты**: HTML использует системный стек 11–14 px; ImGui-порт рисует
  одним размером шрифта (масштабируется через `ui-scale`/`FontGlobalScale`).
* **Переходы вкладок**: fadeIn-анимация вкладки не воспроизводилась
  (ImGui рисует содержимое по-разному); остальные анимации — на месте.
* Ключ `menu-key` для мышинных кнопок отображается, но тумблером меню
  работает только назначенная клавиша клавиатуры (в HTML это было багом).

---

# Deadlock: игровая логика поверх UI

Всё ниже — **educational-only**: чтение/запись собственных структур игры в
однопользовательском режиме, без какого-либо обхода античита. В онлайне не
использовать.

## Правки под Deadlock (вкладка Misc)

Вкладка Misc приведена к тому, что действительно реализуемо в Source 2 без
серверной магии:

* **удалены** строка `Fast Reload`, карточка `Network & Angles` (слайдеры
  *Fake Lag*, *Anti-Aim Angle*) и карточка `Anti-Aim Mode` (Static/Spin/Random);
  из `nexus::State` убраны поля `fastReload`, `fakeLag`, `antiAimAngle`, `aaMode`
  (включая их упоминания в `ResetSettings`);
* **добавлены** в `nexus::State`: `autoActiveReload`, `fovChanger`,
  `fovValue` (70..130, по умолчанию 90);
* новый `DrawMiscTab`: карточка **Exploits** (Bunny Hop, Auto Accept,
  Auto Active Reload) и карточка **View** (FOV Changer + слайдер FOV).

Вкладки Aim / Visuals / Settings не менялись; PSilent не тронут вообще —
тумблеры ESP в Visuals остались как были.

## Реализованная логика

Бэкенд игры — `dll/game_windows.cpp` (в процессе игры) либо `src/game_stub.cpp`
(headless/демо/тесты, всегда «игры нет»). Пока паттерн entity list не найден,
все фичи мягко отключаются, а причина пишется в `%TEMP%\nexus_dll.log`.

### Доступ к сущностям

* модуль `client.dll` — `GetModuleHandleW`, размер — `SizeOfImage` из PE-заголовка;
* сначала используется `CGameEntitySystem::GetBaseEntity` и singleton из
  сигнатур, сверенных с Andromeda-Base; если эти сигнатуры не совпали, включается
  raw-list fallback по трём RIP-паттернам;
* двухуровневый обход Source 2 fallback: `chunk = list + 0x8 + 0x10*(i >> 9)`,
  `entry = chunk + 0x70*(i & 0x1FF)`, сущность = `entry` (layout 0) или
  `*(entry+0x10)` (layout 1). Layout определяется автоматически на индексах
  1..511 поиском «похожего контроллера» (имя — printable-строка,
  `m_iConnected <= 3`, `m_bIsLocalPlayerController ∈ {0,1}`);
* handle → сущность: `idx = handle & 0x3FFF` (`0xFFFFFFFF` — невалиден), тот же обход;
* игрок = контроллер с валидным pawn (`m_hHeroPawn`, иначе `m_hPawn`);
  враг = `team != localTeam && team >= 2 && localTeam >= 2`
  (команды 0/1 — нейтралы/стражи, не рисуются).

### Математика (`src/game.cpp`, покрыта `tools/logic_test.cpp`)

* камера: `pos = originPawn + (0,0,viewOffsetZ)`, pitch/yaw из `m_angClientCamera`,
  `fovX` из `m_iDesiredFOV` (60..140, иначе 90); конвенция Source: yaw 0 = +X,
  pitch «+» = вниз, ось экрана X = `(-sin yaw, cos yaw, 0)`;
* World→Screen: `focal = (W/2)/tan(fovX/2)`, `sx = W/2 + x*focal/z`,
  `sy = H/2 - y*focal/z`; отбраковка `z <= 1` и точек за экраном;
* `CalcAngles`: `pitch = -atan2(z, hyp2d)`, `yaw = atan2(y, x)` (в градусах);
* скелет: индексы `{pelvis 1, spine 3, chest 4, neck 5, head 6, armL 8, armR 14,
  legL 22, legR 25}` (`kBoneIndex`), matrix3x4_t по 0x30, трансляция в колонке 3;
  кость валидна при `|b - origin| < 96` и конечности; весь набор сбрасывается,
  если валидных < 5 или голова ниже таза — тогда ESP/айм переходят на расчётные
  точки из `viewOffset` (без костей).

### ESP (`src/esp.cpp`)

Снапшот `game::Snapshot` (до 64 игроков: valid/alive/isLocal/isEnemy, team,
health/maxHealth, name[64], origin, velocity, viewOffsetZ, boneValid[9]+bone[9],
камера, `weaponInReload`). Рисуется в `GetBackgroundDrawList()`.

**Порядок вызова**: `nexus_esp::Render(snap)` выполняется после `DrawMenu()`;
ESP рисуется поверх FOV-кругов на background draw list и под окном меню
(окна рисуются позже background-листа).

Настройки берутся из `nexus::GetState()`: `esp` (мастер), `skeleton`,
`healthBar`, `nameDist`, `outlineOpacity` (альфа бокса), `drawDistance`
(0..500 м, 1 м = 39.37 юнитов) и палитры зеркально меню: союзники
`#3b82f6/#10b981/#8b5cf6`, враги `#ef4444/#f59e0b/#ec4899`.

Геометрия: ноги = origin, голова = кость head (или `origin+(0,0,vOffset+8)`);
бокс центрирован по X, высота = `feetY - headY` (отбраковка < 6 px),
ширина = 0.60×высоты, толщина 1.5 px. Хелсбар слева (3 px): >50 % зелёный,
>25 % жёлтый, иначе красный. Подпись над боксом `"%s %dm"` с чёрной тенью.
Скелет — связи pelvis–spine–chest–neck–head, chest–armL/R, pelvis–legL/R
(только при обеих валидных костях), белым ~0.9×alpha.

### Векторный аим (`src/aim.cpp`)

Только вкладка Aim, режим **Normal** (`aimType == 0`); PSilent не реализуется.
`nexus_aim::Tick(snapshot)` вызывается каждый кадр; гейты:
`aimAssist && normal.enable && !menuOpen && ImGui::IsKeyDown(activationKey)`.

Клавиша из меню (`"MB4"` по умолчанию) мапится в `ImGuiKey`:
`MB1/MOUSE1→MouseLeft`, `MB2→MouseRight`, `MB3→MouseMiddle`,
`MB4/MOUSE4→MouseX1`, `MB5/MOUSE5→MouseX2`; одиночные буквы/цифры — напрямую;
остальное — сравнением с `ImGui::GetKeyName` без пробелов
(«Left Shift»→`LeftShift`, `SPACE`, `TAB`, `ENTER/RETURN`, `INSERT`, `DELETE`,
`HOME`, `END`, `PAGEUP`, `PAGEDOWN`, `CAPSLOCK`, `F1..F12`). Неизвестное имя →
`ImGuiKey_None`, аим выключен (fail-safe).

Пайплайн: (1) для каждого живого врага — первая принятая точка по чипам
`aimPoints[6]` (Head, Neck, Chest, Arms, Pelvis, Legs): настоящая кость, иначе
расчётная (head = `origin+(0,0,vOffset+6)`, neck = `vOffset-6`, chest = `0.55*vOffset`,
pelvis = `0.35*vOffset`, arms → chest, legs → +8 юнитов); (2) конус =
`normal.fov / 2` (слайдер хранит полный конус), точка принимается при угловом
расстоянии ≤ конуса; (3) **HS Limit**: при включённом тумблере голова допустима
только внутри `cone * (hsLimitVal/100)`, иначе берётся следующий чип;
(4) `predictMovement` → `pos += velocity * (dist / kBulletSpeed)`,
`kBulletSpeed = 1100` юнитов/с; (5) выбор цели по `targetPrio`:
0 Closest = мин. угол, 1 Health = мин. HP, 2 Damage = мин. `HP*(1+dist/1000)`;
(6) сглаживание `factor = 1 - 0.97*(smooth/100)` и `SmoothAngles` с нормировкой
через ±180; (7) `game::SetCameraAngles(pitch, yaw)`.

Запись углов в процессе: пишутся **и** `m_angClientCamera`, **и** `v_angle`
(0x0CC4), **и** `m_angEyeAngles` (по 2 float pitch/yaw).

### Misc

* **FOV Changer**: при включённом `fovChanger` каждый кадр пишется
  `*(int*)(localController + 0x6AC) = (int)fovValue`;
* **Auto Active Reload** (предмет «Active Reload»: R в подсвеченном окне
  перезарядки завершает её мгновенно): при `autoActiveReload &&
  snapshot.weaponInReload` (флаг `m_bInReload` оружия; оружие =
  `*(pawn+0xC20)` → handle `+0x58` → сущность) раз в 90 мс шлётся синтетическое
  нажатие `R` (`keybd_event('R', scan, 0, 0)` + `KEYEVENTF_KEYUP`).

### Что НЕ реализовано (сознательно)

* **Chams** — нужны хуки материалов/шейдеров рендера Source 2; тумблер в Visuals
  остаётся чисто визуальной настройкой;
* **Ignore Obstacles** — трассировки лучей нет, тумблер ни на что не влияет;
* **PSilent** — не тронут по условию;
* обход античита — отсутствует полностью.

## Сборка DLL и инжект

Только Windows + MSVC (цель `nexus`):

```bat
cd nexus_imgui
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
rem → build\Release\nexus.dll
```

Инжект — любым загрузчиком в `deadlock2.exe` после старта матча (или до: поток
сам подождёт `client.dll` до 30 с). Дальше:

* **Insert** — открыть/закрыть меню (клавишу можно переназначить в Settings);
* лог — `%TEMP%\nexus_dll.log` (+ `OutputDebugStringA` для DebugView);
* **выгрузки нет**: vtable `IDXGISwapChain` остаётся пропатченной до выхода
  процесса, `FreeLibrary` не поддерживается (и не должен вызываться).

Как это устроено: `DllMain` на attach создаёт поток → поток ждёт `client.dll`
(≤ 30 с) → `nexus_hooks::Install()`: dummy-окно +
`D3D11CreateDeviceAndSwapChain` (HARDWARE, затем WARP-фолбэк) → патч общей
vtable `IDXGISwapChain` (`Present` = 8, `ResizeBuffers` = 13, через
`VirtualProtect`) → dummy-объекты уничтожаются. Первый `Present`: `GetDesc` →
hwnd, `GetDevice` → device/context, RTV из back-буфера, `ImGui::CreateContext`
(`NavEnableKeyboard`, `IniFilename = nullptr`), шрифт
`C:\Windows\Fonts\segoeui.ttf` 15 px с фолбэком на встроенный,
`ImGui_ImplWin32_Init` + `ImGui_ImplDX11_Init`, подмена `WndProc` через
`SetWindowLongPtrW(GWLP_WNDPROC)`. Кадр: NewFrame бэкендов →
`game::Capture` → `nexus_aim::Tick` → misc-тик → `nexus::DrawMenu` →
`nexus_esp::Render` → `ImGui::Render` → свой RTV → `RenderDrawData` →
восстановление RT/viewport → оригинальный `Present`. `ResizeBuffers`:
освободить RTV → оригинал → пересоздать RTV. `WndProc` сначала отдаёт сообщения
ImGui, а при открытом меню глотает ввод (клавиатура/мышь/`WM_INPUT`). Скан
контроллеров идёт порциями по 128 индексов за кадр с кешированием найденных
индексов, а не двумя полными обходами 2048 сущностей на каждом Present. Раз в
2 секунды логируется статус `Capture` (число игроков, живых, врагов и viewport)
для диагностики. При открытии меню курсор
ImGui отображается (`MouseDrawCursor`) и снимается ограничение `ClipCursor`;
шапку можно перетаскивать, а меню центрируется заново при каждом открытии.

Проверка без MSVC (кросс-сборка, только компиляция/линковка):

```bash
python -m ziglang c++ -target x86_64-windows-gnu -std=c++17 -shared \
  -I <imgui> -I src -I dll -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS \
  <все исходники imgui + backends dx11/win32 + src + dll> \
  -o nexus.dll -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -luser32 -lgdi32 -lshell32
```

## Обновление после патча игры

1. **Оффсеты** — `src/game_offsets.h` (все значения — абсолютные от начала
   объекта; сверяются со schema-дампами `Client.dll.txt` / `sdk/`);
2. **паттерны entity list** — `kEntityListPatterns` в `dll/game_windows.cpp`
   (три кандидата, `FindRip(..., rel=3, len=7)`);
3. **кости** — `kBoneIndex` в `src/game.cpp` плюс валидация
   (`|bone - origin| < 96`, ≥ 5 валидных, голова выше таза) в `ValidateBones`;
4. константы обхода entity list — там же, блок `game::off::kList*`;
5. скорость пули для предикта — `kBulletSpeed` в `src/aim.cpp`.

После правки прогнать `nexus_logic_test` (математика не зависит от дампов, но
тест ловит регрессии) и переснять шоты headless-ом.

## Известные отличия и ограничения

* **Chams** не реализован (нужны хуки материалов) — см. выше;
* **Ignore Obstacles** не влияет на аим (трассировки нет);
* **углы могут перетираться движком**: запись идёт напрямую в память pawn
  (`m_angClientCamera` / `v_angle` / `m_angEyeAngles`). Если в конкретной сборке
  игры клиент пересчитывает их после нас, потребуется хук `CreateMove`/
  `SendMove` — здесь он сознательно не делается;
* **unload не поддерживается** — только выход из игры;
* ESP рисуется под окном меню; за меню в DLL виден кадр игры;
* `m_iDesiredFOV` читается как int и зажимается в 60..140, иначе 90.

## Headless-скриншоты и ESP-демо

```bash
./build/nexus_headless --tab misc --out misc.ppm
./build/nexus_headless --tab aim --fov both --closed --out fov.ppm
./build/nexus_headless --esp-demo --closed --fov normal --out esp.ppm
./build/nexus_headless --esp-demo --out esp_demo.ppm
python3 tools/ppm2png.py esp.ppm shots/esp.png
```

`--esp-demo` собирает фейковый снапшот (камера (0,0,64), yaw 0, fov 90; враг
team 3 «Enemy One» hp 187/260 в левой половине на (600,-460); враг team 3
«Enemy Two» hp 84/240 справа на (1400,1072); союзник team 2 «Ally» на (700,656);
у всех синтетические валидные кости), включает пресет
esp/skeleton/healthBar/nameDist, открывает вкладку Visuals и вызывает
`nexus_esp::Render` **после** `nexus::DrawMenu` внутри кадра. Готовые PNG — в
`shots/`: `misc.png`, `esp.png` (`--esp-demo --closed --fov normal`),
`esp_demo.png` (`--esp-demo`).

Юнит-тесты:

```bash
cmake --build build --target nexus_logic_test && ./build/nexus_logic_test
# → "168 checks, 0 failures" и "ALL PASS"
```
