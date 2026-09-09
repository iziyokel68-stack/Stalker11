"""
☢ STALKER — единое приложение мастера
=========================================
Главное меню (Tkinter) + модули. Один ярлык «STALKER» вместо набора
отдельных .exe/.py — см. docs/PROGRESSION.txt §10, ROADMAP.md ФАЗА 4.

Модули:
  Регистрация     — список игроков события, привязка PDA UID, экспорт CSV
  Программатор    — существующий programmat_pc/programmer.py (subprocess, без изменений)
  Сбор статистики — снимок прогресса игрока в конце игры, экспорт CSV
  Глобальное сообщение / Карта локации — заглушки (протокол §11 не согласован)

Запуск:
    cd stalker_app
    python main.py
"""

import os
import sys
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from db.event_db import EventDB, default_db_path, new_event_id
from modules.registration import RegistrationFrame
from modules.stats import StatsFrame
from modules.stub import StubFrame
from modules import programmer_launcher

APP_TITLE = "STALKER — Приложение мастера"
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "events")


class StalkerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("980x620")
        self.minsize(860, 560)

        os.makedirs(DATA_DIR, exist_ok=True)

        self.db: EventDB = None
        self.event_id: str = None
        self.event_title: str = None

        self.container = ttk.Frame(self)
        self.container.pack(fill="both", expand=True)

        self._pick_or_create_event()
        self.show_menu()

    # ------------------------------------------------------------------
    # Событие (SQLite stalker_event_<id>.db, §10.2)
    # ------------------------------------------------------------------

    def _pick_or_create_event(self):
        """При старте: выбрать существующий .db в events/ или создать новый."""
        existing = [f for f in os.listdir(DATA_DIR) if f.endswith(".db")]

        if not existing:
            self._create_new_event()
            return

        choice = messagebox.askyesno(
            "Событие",
            f"Найдено {len(existing)} сохранённых событий.\n"
            "Открыть последнее? (Нет — создать новое)",
        )
        if choice:
            existing.sort(reverse=True)
            path = os.path.join(DATA_DIR, existing[0])
            self.db = EventDB(path)
            events = self.db.list_events()
            if events:
                self.event_id = events[0]["event_id"]
                self.event_title = events[0]["title"]
            else:
                self._create_new_event()
        else:
            self._create_new_event()

    def _create_new_event(self):
        title = simpledialog.askstring(
            "Новое событие", "Название события/игрового дня:",
            initialvalue="Событие " + __import__("time").strftime("%Y-%m-%d"),
        ) or "Событие"
        event_id = new_event_id()
        path = default_db_path(event_id, DATA_DIR)
        self.db = EventDB(path)
        self.db.create_event(title, event_id)
        self.event_id = event_id
        self.event_title = title

    # ------------------------------------------------------------------
    # Навигация
    # ------------------------------------------------------------------

    def _clear_container(self):
        for child in self.container.winfo_children():
            child.destroy()

    def show_menu(self):
        self._clear_container()
        frame = ttk.Frame(self.container)
        frame.pack(fill="both", expand=True)

        header = ttk.Frame(frame)
        header.pack(fill="x", padx=20, pady=(20, 10))
        ttk.Label(header, text="☢ STALKER", font=("Segoe UI", 22, "bold")).pack(anchor="w")
        ttk.Label(header, text=f"Событие: {self.event_title}  ({self.event_id})",
                  foreground="#666").pack(anchor="w")
        ttk.Button(header, text="Сменить / создать событие", command=self._switch_event).pack(
            anchor="w", pady=(6, 0))

        menu = ttk.Frame(frame)
        menu.pack(fill="both", expand=True, padx=20, pady=10)

        buttons = [
            ("Регистрация", "Список игроков, привязка PDA, экспорт CSV",
             self.show_registration),
            ("Программатор", "Чипы, аномалии, убежища, ПДА, терминалы (programmer.py)",
             self.launch_programmer),
            ("Сбор статистики", "Снимок прогресса игрока в конце игры",
             self.show_stats),
            ("Глобальное сообщение", "Мастер-Пульт → LoRa → ПДА (протокол в разработке)",
             self.show_broadcast_stub),
            ("Карта локации", "Схема полигона, якоря UWB, зоны (в разработке)",
             self.show_map_stub),
        ]
        for i, (label, desc, cmd) in enumerate(buttons):
            card = ttk.Frame(menu, relief="groove", borderwidth=1)
            card.grid(row=i // 2, column=i % 2, padx=8, pady=8, sticky="nsew")
            ttk.Label(card, text=label, font=("Segoe UI", 14, "bold")).pack(
                anchor="w", padx=12, pady=(10, 2))
            ttk.Label(card, text=desc, foreground="#666", wraplength=380,
                      justify="left").pack(anchor="w", padx=12, pady=(0, 8))
            ttk.Button(card, text="Открыть", command=cmd).pack(
                anchor="w", padx=12, pady=(0, 10))

        for c in range(2):
            menu.columnconfigure(c, weight=1)

    def _switch_event(self):
        self._create_new_event()
        self.show_menu()

    def show_registration(self):
        self._clear_container()
        RegistrationFrame(self.container, self.db, self.event_id,
                           on_back=self.show_menu).pack(fill="both", expand=True)

    def show_stats(self):
        self._clear_container()
        StatsFrame(self.container, self.db, self.event_id,
                   on_back=self.show_menu).pack(fill="both", expand=True)

    def show_broadcast_stub(self):
        self._clear_container()
        StubFrame(
            self.container, "Глобальное сообщение",
            "Доставка Мастер-Пульт → LoRa → ПДА (не через терминалы кассы/банка).\n"
            "Доступ игрокам — ур.50+ (docs/PROGRESSION.txt §4, §11).\n"
            "Протокол ещё не согласован — см. ROADMAP.md ФАЗА 5.",
            on_back=self.show_menu,
        ).pack(fill="both", expand=True)

    def show_map_stub(self):
        self._clear_container()
        StubFrame(
            self.container, "Карта локации",
            "Схема полигона, якоря UWB, зоны безопасности (docs/PROGRESSION.txt §11).\n"
            "Отдельная спецификация — после стабилизации фаз 2-3.",
            on_back=self.show_menu,
        ).pack(fill="both", expand=True)

    def launch_programmer(self):
        ok, msg = programmer_launcher.launch_programmer()
        if ok:
            messagebox.showinfo("Программатор", msg)
        else:
            messagebox.showwarning("Программатор", msg)

    def destroy(self):
        if self.db:
            self.db.close()
        super().destroy()


def main():
    app = StalkerApp()
    app.mainloop()


if __name__ == "__main__":
    main()
