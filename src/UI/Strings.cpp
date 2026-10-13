#include "Strings.hpp"

#include <cstring>

namespace str {

namespace {

struct Entry {
    const char* key;
    int         max_width;   // cells; 0 = unconstrained
    const char* en;
    const char* ru;
};

// One table, three columns: the key (for diagnostics), the width budget the
// layout can actually afford, and the two locales. Adding a locale is one more
// column plus one line in kLocales — which is the whole point of doing this
// before the widgets exist.
const Entry kTable[ID_COUNT] = {
    { "app.title",       24, "CONUI · PLAYGROUND", "CONUI · ПЛОЩАДКА"   },
    { "theme.editor",    20, "THEME EDITOR",       "РЕДАКТОР ТЕМ"       },
    { "menu.file",        9, "FILE",               "ФАЙЛ"               },
    { "menu.view",        9, "VIEW",               "ВИД"                },
    { "menu.widgets",    11, "WIDGETS",            "ЭЛЕМЕНТЫ"           },
    { "menu.theme",       9, "THEME",              "ТЕМА"               },
    { "menu.help",        9, "HELP",               "СПРАВКА"            },

    { "action.run",       8, "RUN",                "ЗАПУСК"             },
    { "action.stop",      8, "STOP",               "СТОП"               },
    { "action.reset",     8, "RESET",              "СБРОС"              },
    { "action.save",      8, "SAVE",               "СОХР."              },
    { "action.open",      8, "OPEN",               "ОТКРЫТЬ"            },
    { "action.cancel",    8, "CANCEL",             "ОТМЕНА"             },
    { "action.delete",    8, "DELETE",             "УДАЛИТЬ"            },
    { "action.fault",     8, "FAULT",              "СБОЙ"               },

    { "label.opacity",   10, "OPACITY",            "ПРОЗРАЧН."          },
    { "label.panel_op",  10, "PANELS",             "ПАНЕЛИ"             },
    { "label.build",     10, "BUILD",              "СБОРКА"             },
    { "label.preview",   10, "PREVIEW",            "ПРЕВЬЮ"             },
    { "label.locale",    10, "LOCALE",             "ЯЗЫК"               },
    { "label.ready",      9, "READY",              "ГОТОВО"             },

    { "state.on",         4, "ON",                 "ВКЛ"                },
    { "state.off",        4, "OFF",                "ВЫКЛ"               },
    { "label.blur",      10, "BLUR",               "РАЗМЫТИЕ"           },
    { "label.density",   10, "DENSITY",            "ПЛОТНОСТЬ"          },
    { "density.compact", 10, "Compact",            "Плотная"            },
    { "density.normal",  10, "Normal",             "Обычная"            },
    { "density.large",   10, "Large",              "Крупная"            },
    { "opt.transparent", 24, "Transparent background", "Прозрачный фон" },
    { "opt.grid",        24, "Show cell grid",     "Показать сетку"     },
    { "opt.autosave",    24, "Autosave",           "Автосохранение"     },
    { "opt.glitch",      24, "Glitch effects",     "Эффекты глитча"     },
    { "label.range",     10, "RANGE",              "ДИАПАЗОН"           },
    { "label.steps",     10, "STEPS",              "ШАГИ"               },
    { "label.tests",     10, "TESTS",              "ТЕСТЫ"              },
    { "label.pending",   10, "PENDING",            "ОЖИДАНИЕ"           },
    { "label.rows",       6, "rows",               "строк"              },
    { "label.name",       8, "NAME",               "ИМЯ"                },
    { "label.search",     8, "SEARCH",             "ПОИСК"              },
    { "label.port",       8, "PORT",               "ПОРТ"               },
    { "ph.search",       22, "search by name…",    "поиск по имени…"    },
    { "label.notes",     12, "NOTES",              "ЗАМЕТКИ"            },
    { "label.processes", 12, "PROCESSES",          "ПРОЦЕССЫ"           },
    { "label.filesystem",12, "FILESYSTEM",         "ФАЙЛЫ"              },
    { "col.pid",          6, "PID",                "PID"                },
    { "col.cpu",          6, "CPU",                "ЦП"                 },
    { "col.mem",          9, "MEM",                "ПАМЯТЬ"             },
    { "col.state",        9, "STATE",              "СОСТОЯН."           },
    { "fmt.ln_col",      18, "LN %d, COL %d",      "СТР %d, КОЛ %d"     },
    { "fmt.n_lines",     14, "%d lines",           "строк: %d"          },
    { "state.ovr",        4, "OVR",                "ЗАМ"                },
    { "tab.input",       10, "INPUT",              "ВВОД"               },
    { "modal.confirm",   16, "CONFIRM",            "ПОДТВЕРЖДЕНИЕ"      },
    { "modal.confirm_q", 36, "Delete 3 selected items?", "Удалить 3 выбранных элемента?" },
    { "modal.note",      36, "This action cannot be undone.", "Это действие нельзя отменить." },
    { "action.delete_d", 10, "DELETE…",            "УДАЛИТЬ…"           },
    { "menu.open",       16, "Open",               "Открыть"            },
    { "menu.rename",     16, "Rename…",            "Переименовать…"     },
    { "menu.copy_path",  16, "Copy path",          "Копировать путь"    },
    { "menu.hidden",     16, "Show hidden",        "Скрытые файлы"      },
    { "menu.sort_by",    16, "Sort by",            "Сортировать"        },
    { "menu.by_name",    16, "Name",               "По имени"           },
    { "menu.by_size",    16, "Size",               "По размеру"         },
    { "menu.by_date",    16, "Date",               "По дате"            },
    { "menu.delete",     16, "Delete",             "Удалить"            },
    // Multi-line: the width budget applies to each line (see the self-test).
    { "tip.opacity",     26, "Window background opacity.\nShift — fine step.",
                             "Непрозрачность фона окна.\nShift — точный шаг." },
    { "tip.panel_op",    28, "Section background opacity.\nFrames and text stay solid.",
                             "Непрозрачность фона секций.\nРамки и текст не меняются." },
    { "ed.new",           8, "NEW",                "НОВАЯ"              },
    { "ed.revert",        8, "REVERT",             "ОТКАТ"              },
    { "ed.file",          6, "FILE",               "ФАЙЛ"               },
    { "ed.modified",     12, "● modified",         "● изменена"         },
    { "ed.saved",        12, "✓ saved",            "✓ сохранена"        },
    { "ed.slot",          8, "SLOT",               "СЛОТ"               },
    { "ed.preview",      16, "LIVE PREVIEW",       "ЖИВОЕ ПРЕВЬЮ"       },
    { "ed.slots",        12, "%d SLOTS",           "СЛОТОВ: %d"         },
    { "ed.changed",      16, "%d changed",         "изменено: %d"       },
    { "ed.hint",         32, "Ctrl+S save · Esc close", "Ctrl+S сохранить · Esc закрыть" },
    { "ed.pv_sel",       18, "selected row",       "выделенная строка"  },
    { "ed.pv_dither",    10, "dither",             "дизеринг"           },
    { "ed.pv_danger",    10, "danger",             "опасно"             },
    { "ed.open",         14, "THEME EDITOR…",      "РЕДАКТОР ТЕМ…"      },
    { "view.workspace",  16, "Workspace",          "Рабочее место"      },
    { "view.states",     16, "States",             "Состояния"          },
    { "view.glyphs",     16, "Glyphs",             "Символы"            },
    { "menu.fullscreen", 16, "Fullscreen",         "Полный экран"       },
    { "menu.quit",       16, "Quit",               "Выход"              },
    { "menu.about",      16, "About",              "О программе"        },
    { "menu.editor",     16, "Theme editor…",      "Редактор тем…"      },
    { "menu.tab_ctrl",   18, "Controls",           "Элементы"           },
    { "menu.tab_input",  18, "Input",              "Ввод"               },
    { "menu.tab_set",    18, "Settings",           "Настройки"          },
    { "tab.settings",    10, "SETTINGS",           "НАСТРОЙКИ"          },
    { "build.compile",   10, "COMPILE",            "КОМПИЛЯЦ."          },
    { "build.resources", 10, "RESOURCES",          "РЕСУРСЫ"            },
    { "build.link",      10, "LINK",               "КОМПОНОВКА"         },
    { "label.states",    12, "STATES",             "СОСТОЯНИЯ"          },
    { "st.widget",       12, "WIDGET",             "ЭЛЕМЕНТ"            },
    { "st.idle",         10, "IDLE",               "ПОКОЙ"              },
    { "st.hover",        10, "HOVER",              "НАВЕДЕНИЕ"          },
    { "st.active",       10, "ACTIVE",             "НАЖАТ"              },
    { "st.focus",        10, "FOCUS",              "ФОКУС"              },
    { "st.disabled",     10, "DISABLED",           "ОТКЛЮЧЁН"           },
    { "st.error",        10, "ERROR",              "ОШИБКА"             },
    { "w.button",        12, "Button",             "Кнопка"             },
    { "w.default",       12, "Default",            "По умолч."          },
    { "w.danger",        12, "Danger",             "Опасная"            },
    { "w.checkbox",      12, "Checkbox",           "Флажок"             },
    { "w.radio",         12, "Radio",              "Переключ."          },
    { "w.switch",        12, "Switch",             "Выключат."          },
    { "w.slider",        12, "Slider",             "Ползунок"           },
    { "w.text",          12, "Text field",         "Поле ввода"         },
    { "w.dropdown",      12, "Dropdown",           "Список"             },
    { "label.vocab",     12, "VOCABULARY",         "СЛОВАРЬ"            },
    { "label.solid",     16, "SOLID FIELD",        "СПЛОШНОЕ ПОЛЕ"      },
    { "label.shades",    12, "SHADES",             "ТЕНИ"               },
    { "hint.f1",         12, "F1 help",            "F1 справка"         },
    { "hint.quitkey",    14, "Ctrl+Q quit",        "Ctrl+Q выход"       },
    { "fmt.metrics",     40, "%d FPS · 1 draw call · %d verts", "%d к/с · 1 вызов · %d вершин" },
    { "about.title",     14, "ABOUT",              "О ПРОГРАММЕ"        },
    { "about.1",         48, "ConUI — a desktop UI drawn like a console.", "ConUI — десктопный UI, нарисованный как консоль." },
    { "about.2",         48, "One grid, one font, one draw call.", "Одна сетка, один шрифт, один вызов отрисовки." },
    { "about.3",         48, "F1 help · F11 fullscreen · Ctrl+Q quit", "F1 справка · F11 весь экран · Ctrl+Q выход" },
    { "action.ok",        8, "OK",                 "ОК"                 },

    { "hint.fullscreen", 22, "F11 fullscreen",     "F11 полный экран"   },

    { "et.insert_row",   16, "Insert row",         "Вставить строку"    },
    { "et.delete_row",   16, "Delete row",         "Удалить строку"     },
    { "label.tasks",     12, "TASKS",              "ЗАДАЧИ"             },
    { "col.task",        10, "TASK",               "ЗАДАЧА"             },
    { "col.owner",       10, "OWNER",              "КТО"                },
    { "col.notes",       10, "NOTES",              "ЗАМЕТКИ"            },
    { "density.tiny",    10, "Tiny",               "Мелкая"             },
    { "view.charts",     16, "Charts",             "Графики"            },
    { "chart.traffic",   14, "TRAFFIC",            "ТРАФИК"             },
    { "chart.cpu",       14, "CPU LOAD",           "ЗАГРУЗКА CPU"       },
    { "chart.weekly",    14, "COMMITS",            "КОММИТЫ"            },
    { "chart.disk",      14, "DISK USAGE",         "ДИСКИ"              },
    { "view.diagrams",   16, "Diagrams",           "Диаграммы"          },
    { "diag.hint",       70, "wheel: zoom · drag: move · double-click: reset", "колесо: масштаб · перетаскивание: сдвиг · двойной щелчок: сброс" },
    { "diag.tree",       24, "EXPRESSION TREE",    "ДЕРЕВО ВЫРАЖЕНИЯ"   },
    { "diag.net",        24, "NETWORK",            "НЕЙРОСЕТЬ"          },
};

const char* const kLocaleCodes[LOCALE_COUNT] = { "en", "ru" };
const char* const kLocaleNames[LOCALE_COUNT] = { "English", "Русский" };

int g_locale = LOCALE_EN;

inline bool valid(Id id) { return id >= 0 && id < ID_COUNT; }

} // namespace

void SetLocale(int locale) {
    if (locale >= 0 && locale < LOCALE_COUNT) g_locale = locale;
}

int GetLocale() { return g_locale; }

const char* LocaleCode(int locale) {
    return (locale >= 0 && locale < LOCALE_COUNT) ? kLocaleCodes[locale] : kLocaleCodes[0];
}

int LocaleFromCode(const char* code) {
    if (!code) return LOCALE_EN;
    for (int i = 0; i < LOCALE_COUNT; ++i)
        if (std::strcmp(code, kLocaleCodes[i]) == 0) return i;
    return LOCALE_EN;
}

const char* LocaleName(int locale) {
    return (locale >= 0 && locale < LOCALE_COUNT) ? kLocaleNames[locale] : kLocaleNames[0];
}

const char* T(Id id, int locale) {
    if (!valid(id)) return "?";
    const Entry& e = kTable[id];
    const char* s = nullptr;
    switch (locale) {
    case LOCALE_RU: s = e.ru; break;
    default:        s = e.en; break;
    }
    // A missing or empty translation falls back to English rather than showing
    // a blank: a half-finished locale should degrade, not break.
    if (!s || !*s) s = e.en;
    return s ? s : "?";
}

const char* T(Id id) { return T(id, g_locale); }

int MaxWidth(Id id) { return valid(id) ? kTable[id].max_width : 0; }

const char* Key(Id id) { return valid(id) ? kTable[id].key : "?"; }

} // namespace str
