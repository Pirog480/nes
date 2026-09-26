# nes

* `uusa.html` — исходный одностраничный UI «Cheat Menu UI» (HTML/CSS/JS, тёмная тема, вкладки Aim / Visuals / Heroes / Misc / Settings).
* [`nexus_imgui/`](nexus_imgui/) — **порт `uusa.html` на Dear ImGui (C++)**:
  * `src/nexus_menu.*`, `src/nexus_widgets.*` — перенос всего UI (темы, слайдеры, тумблеры, свотчи, keybind-захват, hero-панель, FOV-круги, тосты, drag, анимации) — зависит только от `<imgui.h>`;
  * `src/main.cpp` — демо под GLFW + OpenGL3;
  * `tools/` — headless-рендер (софтверный растеризатор ImDrawData → PPM/PNG) и конвертер;
  * `shots/` — готовые скриншоты всех вкладок (тёмная/светлая тема, hero-панель, FOV, тост).

  Сборка: `cmake -B build && cmake --build build -j` (см. [`nexus_imgui/README.md`](nexus_imgui/README.md)).
