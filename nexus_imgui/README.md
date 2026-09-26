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
| Анимированные фоновые «блобы» | Кольцевая заливка на background draw list (имитация blur 100px) |
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

* `NEXUS_BUILD_DEMO=ON|OFF` — демо-окно (нужны X11/Wayland + OpenGL).
* `NEXUS_BUILD_TOOLS=ON|OFF` — headless-инструмент (собирается везде).

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
   (или не используйте — меню само рисует фон/блобы на background draw list).
5. Состояние всех настроек — `nexus::GetState()` (структура `nexus::State`).

## Устройство кода

```
src/nexus_widgets.h/.cpp — переиспользуемые контролы (toggle, slider, seg...)
src/nexus_menu.h/.cpp    — состояние, тема, вкладки, оверлеи, ввод
src/main.cpp             — демо-оболочка GLFW+OpenGL3 (не для интеграции)
tools/headless_render.cpp— рендер ImDrawData софтвером → PPM (скриншоты)
tools/ppm2png.py         — PPM → PNG без зависимостей
```

## Известные отличия от оригинала

* **Blur/glass**: в ImGui нет `backdrop-filter` и SVG-фильтров — фон
  полупрозрачный, «блобы» имитируются концентрическими кольцами.
* **FPS-счётчик**: в HTML был случайный (135–154), здесь — реальный FPS.
* **Шрифты**: HTML использует системный стек 11–14 px; ImGui-порт рисует
  одним размером шрифта (масштабируется через `ui-scale`/`FontGlobalScale`).
* **Переходы вкладок**: fadeIn-анимация вкладки не воспроизводилась
  (ImGui рисует содержимое по-разному); остальные анимации — на месте.
* Ключ `menu-key` для мышинных кнопок отображается, но тумблером меню
  работает только назначенная клавиша клавиатуры (в HTML это было багом).
