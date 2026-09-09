"""
☢ STALKER — приложение мастера
================================
Одно окно: событие, регистрация, список участников, программатор,
оповещения, доска заданий, статистика, команды, карта.

Запуск:
    cd stalker_app
    python main.py
"""

import os
import sys
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog, filedialog

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from db.event_db import EventDB, default_db_path, new_event_id, list_event_files
from modules.dashboard import DashboardFrame
from modules.registration import RegistrationFrame
from modules.stats import StatsFrame
from modules.broadcast import BroadcastFrame
from modules.quests import QuestsFrame
from modules.commands import CommandsFrame
from modules.mapview import MapFrame
from modules.programmer import ProgrammerFrame
from shared.serial_session import SerialSession
from theme import apply_theme, BG

APP_TITLE = "STALKER — Приложение мастера"
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "events")

NAV_ITEMS = [
    ("dashboard", "Событие"),
    ("registration", "Участники"),
    ("programmer", "Программатор"),
    ("broadcast", "Оповещения"),
    ("quests", "Задания"),
    ("stats", "Статистика"),
    ("commands", "Команды"),
    ("map", "Карта"),
]


class StalkerApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1180x720")
        self.minsize(960, 600)
        apply_theme(self)

        os.makedirs(DATA_DIR, exist_ok=True)

        self.db: EventDB = None
        self.event_id: str = None
        self.event_title: str = None
        self.serial = SerialSession()
        self._nav_buttons = {}
        self._current = None

        self._build_chrome()
        self._pick_or_create_event()
        self.show_dashboard()
        self.after(800, self._poll_ports)

    def _build_chrome(self):
        root = ttk.Frame(self)
        root.pack(fill="both", expand=True)

        sidebar = ttk.Frame(root, style="Panel.TFrame", width=200)
        sidebar.pack(side="left", fill="y")
        sidebar.pack_propagate(False)

        ttk.Label(sidebar, text="☢ STALKER", style="Title.TLabel").pack(
            anchor="w", padx=16, pady=(18, 4))
        self.lbl_event = ttk.Label(sidebar, text="", style="Dim.TLabel", wraplength=170)
        self.lbl_event.pack(anchor="w", padx=16, pady=(0, 12))

        for key, label in NAV_ITEMS:
            btn = ttk.Button(sidebar, text=label, style="Nav.TButton",
                             command=lambda k=key: self._nav(k))
            btn.pack(fill="x", padx=8, pady=2)
            self._nav_buttons[key] = btn

        self.container = ttk.Frame(root)
        self.container.pack(side="left", fill="both", expand=True)

        status = ttk.Frame(self, style="Status.TFrame")
        status.pack(fill="x", side="bottom")
        self.lbl_serial = ttk.Label(status, text="", style="Status.TLabel")
        self.lbl_serial.pack(side="left", padx=10, pady=6)
        ttk.Button(status, text="Подключить USB", command=self._connect_serial).pack(
            side="right", padx=6, pady=4)
        ttk.Button(status, text="Отключить", command=self._disconnect_serial).pack(
            side="right", padx=6, pady=4)
        self.cmb_port = ttk.Combobox(status, width=16, state="readonly")
        self.cmb_port.pack(side="right", padx=6, pady=4)
        self._refresh_ports()

    def _refresh_ports(self):
        ports = self.serial.port_names()
        self.cmb_port["values"] = ports
        if ports and not self.cmb_port.get():
            self.cmb_port.set(ports[0])
        self._update_serial_label()

    def _update_serial_label(self):
        self.lbl_serial.configure(text="USB  " + self.serial.status_text())

    def _connect_serial(self):
        port = self.cmb_port.get() or "AUTO"
        ok, msg = self.serial.connect(port)
        self._update_serial_label()
        if ok:
            messagebox.showinfo("USB", msg, parent=self)
        else:
            messagebox.showwarning("USB", msg, parent=self)

    def _disconnect_serial(self):
        self.serial.disconnect()
        self._update_serial_label()

    def refresh_chrome(self):
        closed = ""
        if self.db and self.db.is_closed(self.event_id):
            closed = " [закрыто]"
        self.lbl_event.configure(
            text=f"{self.event_title}{closed}\n{self.event_id}"
        )
        self.title(f"{APP_TITLE} — {self.event_title}")
        self._update_serial_label()

    def _highlight_nav(self, key):
        for k, btn in self._nav_buttons.items():
            btn.configure(style="NavSel.TButton" if k == key else "Nav.TButton")

    def _clear_container(self):
        for child in self.container.winfo_children():
            child.destroy()

    def _nav(self, key):
        dispatch = {
            "dashboard": self.show_dashboard,
            "registration": self.show_registration,
            "programmer": self.show_programmer,
            "broadcast": self.show_broadcast,
            "quests": self.show_quests,
            "stats": self.show_stats,
            "commands": self.show_commands,
            "map": self.show_map,
        }
        dispatch[key]()

    def _pick_or_create_event(self):
        existing = list_event_files(DATA_DIR)
        if not existing:
            self._create_new_event()
            return
        self._open_db(existing[0])

    def _open_db(self, path):
        if self.db:
            self.db.close()
        self.db = EventDB(path)
        events = self.db.list_events()
        if events:
            self.event_id = events[0]["event_id"]
            self.event_title = events[0]["title"]
        else:
            self._create_new_event()
            return
        self.refresh_chrome()

    def _create_new_event(self, title=None):
        if not title:
            title = simpledialog.askstring(
                "Новое событие", "Название события / игрового дня:",
                initialvalue="Событие " + __import__("time").strftime("%Y-%m-%d"),
                parent=self,
            ) or "Событие"
        event_id = new_event_id()
        path = default_db_path(event_id, DATA_DIR)
        if self.db:
            self.db.close()
        self.db = EventDB(path)
        self.db.create_event(title, event_id)
        self.event_id = event_id
        self.event_title = title
        self.refresh_chrome()

    def create_event_dialog(self):
        self._create_new_event()
        self.show_dashboard()

    def pick_event(self):
        files = list_event_files(DATA_DIR)
        if not files:
            messagebox.showinfo("Событие", "Нет сохранённых баз в stalker_app/events/",
                                parent=self)
            return
        win = tk.Toplevel(self)
        win.title("Открыть событие")
        win.configure(bg=BG)
        win.geometry("520x360")
        lb = tk.Listbox(win, bg="#252732", fg="#e6e6ea", selectbackground="#4a3710")
        lb.pack(fill="both", expand=True, padx=10, pady=10)
        rows = []
        for path in files:
            try:
                tmp = EventDB(path)
                ev = tmp.list_events()
                tmp.close()
                title = ev[0]["title"] if ev else os.path.basename(path)
            except Exception:
                title = os.path.basename(path)
            rows.append(path)
            lb.insert("end", f"{title}  —  {os.path.basename(path)}")
        if rows:
            lb.selection_set(0)

        def ok():
            sel = lb.curselection()
            if not sel:
                return
            self._open_db(rows[sel[0]])
            win.destroy()
            self.show_dashboard()

        ttk.Button(win, text="Открыть", style="Accent.TButton", command=ok).pack(
            pady=(0, 10))

    def import_event_dialog(self):
        path = filedialog.askopenfilename(
            title="Импорт базы события",
            filetypes=[("SQLite", "*.db"), ("Все", "*.*")],
            parent=self,
        )
        if not path:
            return
        dest = EventDB.import_db_copy(path, DATA_DIR)
        self._open_db(dest)
        self.show_dashboard()
        messagebox.showinfo("Импорт", f"Открыто:\n{dest}", parent=self)

    def show_dashboard(self):
        self._current = "dashboard"
        self._highlight_nav("dashboard")
        self._clear_container()
        DashboardFrame(
            self.container, self,
            on_goto={
                "registration": self.show_registration,
                "broadcast": self.show_broadcast,
                "quests": self.show_quests,
                "programmer": self.show_programmer,
                "stats": self.show_stats,
                "commands": self.show_commands,
            },
        ).pack(fill="both", expand=True)

    def show_registration(self):
        self._current = "registration"
        self._highlight_nav("registration")
        self._clear_container()
        RegistrationFrame(
            self.container, self.db, self.event_id,
            serial=self.serial, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_stats(self):
        self._current = "stats"
        self._highlight_nav("stats")
        self._clear_container()
        StatsFrame(
            self.container, self.db, self.event_id,
            serial=self.serial, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_broadcast(self):
        self._current = "broadcast"
        self._highlight_nav("broadcast")
        self._clear_container()
        BroadcastFrame(
            self.container, self.db, self.event_id,
            serial=self.serial, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_quests(self):
        self._current = "quests"
        self._highlight_nav("quests")
        self._clear_container()
        QuestsFrame(
            self.container, self.db, self.event_id, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_commands(self):
        self._current = "commands"
        self._highlight_nav("commands")
        self._clear_container()
        CommandsFrame(
            self.container, self.db, self.event_id,
            serial=self.serial, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_map(self):
        self._current = "map"
        self._highlight_nav("map")
        self._clear_container()
        MapFrame(
            self.container, self.db, self.event_id, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def show_programmer(self):
        self._current = "programmer"
        self._highlight_nav("programmer")
        self._clear_container()
        ProgrammerFrame(
            self.container, serial=self.serial, on_back=self.show_dashboard,
        ).pack(fill="both", expand=True)

    def _poll_ports(self):
        self._refresh_ports()
        self.after(1000, self._poll_ports)

    def destroy(self):
        if self.db:
            self.db.close()
        self.serial.disconnect()
        super().destroy()


def main():
    app = StalkerApp()
    app.mainloop()


if __name__ == "__main__":
    main()
