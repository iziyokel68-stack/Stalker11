"""Дашборд события: счётчики игроков, быстрые действия, статус события."""

import time
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog, filedialog

from theme import module_header


def _fmt_ts(ts):
    if not ts:
        return "—"
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(ts))


class DashboardFrame(ttk.Frame):
    def __init__(self, master, app, on_goto=None):
        super().__init__(master)
        self.app = app
        self.on_goto = on_goto or {}
        self._build()
        self.refresh()

    def _build(self):
        module_header(self, "Событие")
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        info = ttk.LabelFrame(body, text="Текущее событие")
        info.pack(fill="x", pady=(0, 10))
        self.lbl_title = ttk.Label(info, text="", style="Accent.TLabel")
        self.lbl_title.pack(anchor="w", padx=10, pady=(8, 2))
        self.lbl_meta = ttk.Label(info, text="", style="Dim.TLabel")
        self.lbl_meta.pack(anchor="w", padx=10, pady=(0, 8))

        btns = ttk.Frame(info)
        btns.pack(fill="x", padx=10, pady=(0, 10))
        ttk.Button(btns, text="Открыть другое…", command=self.app.pick_event).pack(
            side="left", padx=(0, 6))
        ttk.Button(btns, text="Создать новое", command=self.app.create_event_dialog).pack(
            side="left", padx=(0, 6))
        ttk.Button(btns, text="Переименовать", command=self._rename).pack(
            side="left", padx=(0, 6))
        ttk.Button(btns, text="Экспорт .db (USB)", command=self._export_db).pack(
            side="left", padx=(0, 6))
        ttk.Button(btns, text="Импорт .db", command=self.app.import_event_dialog).pack(
            side="left", padx=(0, 6))
        self.btn_close = ttk.Button(btns, text="Закрыть событие",
                                    command=self._toggle_close)
        self.btn_close.pack(side="left")

        stats = ttk.Frame(body)
        stats.pack(fill="x", pady=(0, 10))
        self.cards = {}
        for i, (key, label) in enumerate((
            ("total", "Игроков"),
            ("registered", "Зарегистрировано"),
            ("admitted", "Допущено"),
            ("bound", "ПДА привязано"),
        )):
            card = ttk.LabelFrame(stats, text=label)
            card.grid(row=0, column=i, padx=4, sticky="nsew")
            stats.columnconfigure(i, weight=1)
            val = ttk.Label(card, text="0", style="Title.TLabel")
            val.pack(padx=12, pady=10)
            self.cards[key] = val

        quick = ttk.LabelFrame(body, text="Быстрый переход")
        quick.pack(fill="x", pady=(0, 10))
        qrow = ttk.Frame(quick)
        qrow.pack(fill="x", padx=10, pady=8)
        for key, label in (
            ("registration", "Список участников"),
            ("broadcast", "Оповещения"),
            ("quests", "Доска заданий"),
            ("programmer", "Программатор"),
            ("stats", "Статистика"),
            ("commands", "Команды мастера"),
        ):
            ttk.Button(qrow, text=label,
                       command=lambda k=key: self._goto(k)).pack(side="left", padx=4)

        hist = ttk.LabelFrame(body, text="Последние оповещения")
        hist.pack(fill="both", expand=True)
        self.bcast = tk.Listbox(hist, height=8, bg="#252732", fg="#e6e6ea",
                                highlightthickness=0, borderwidth=0,
                                selectbackground="#4a3710")
        self.bcast.pack(fill="both", expand=True, padx=8, pady=8)

    def _goto(self, key):
        fn = self.on_goto.get(key)
        if fn:
            fn()

    def refresh(self):
        ev = self.app.db.get_event(self.app.event_id)
        title = ev["title"] if ev else self.app.event_title
        closed = bool(ev and ev["closed_at"])
        self.lbl_title.configure(text=title + ("  [ЗАКРЫТО]" if closed else ""))
        created = _fmt_ts(ev["created_at"]) if ev else "—"
        closed_s = _fmt_ts(ev["closed_at"]) if ev and ev["closed_at"] else "идёт"
        self.lbl_meta.configure(
            text=f"ID: {self.app.event_id}    создано: {created}    статус: {closed_s}"
        )
        self.btn_close.configure(
            text="Открыть снова" if closed else "Закрыть событие"
        )
        counts = self.app.db.player_counts(self.app.event_id)
        for k, lbl in self.cards.items():
            lbl.configure(text=str(counts.get(k, 0)))
        self.bcast.delete(0, "end")
        for b in self.app.db.list_broadcasts(self.app.event_id, limit=12):
            t = time.strftime("%H:%M", time.localtime(b.created_at))
            self.bcast.insert("end", f"{t}  [{b.kind}/{b.status}]  {b.text}")

    def _rename(self):
        title = simpledialog.askstring(
            "Событие", "Название:", initialvalue=self.app.event_title, parent=self
        )
        if not title:
            return
        self.app.db.rename_event(self.app.event_id, title)
        self.app.event_title = title.strip()
        self.app.refresh_chrome()
        self.refresh()

    def _toggle_close(self):
        if self.app.db.is_closed(self.app.event_id):
            self.app.db.reopen_event(self.app.event_id)
        else:
            if not messagebox.askyesno(
                "Закрыть событие",
                "Закрыть игровой день? Регистрацию можно будет открыть снова.",
                parent=self,
            ):
                return
            self.app.db.close_event(self.app.event_id)
        self.refresh()

    def _export_db(self):
        path = filedialog.asksaveasfilename(
            title="Копия базы события на USB",
            defaultextension=".db",
            filetypes=[("SQLite", "*.db")],
            initialfile=f"stalker_event_{self.app.event_id}.db",
            parent=self,
        )
        if not path:
            return
        self.app.db.export_db_copy(path)
        messagebox.showinfo("Экспорт", f"Сохранено:\n{path}", parent=self)
