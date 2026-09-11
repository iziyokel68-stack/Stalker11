"""
Глобальные оповещения мастера.
Доставка: stalker_app → USB стола мастера (CHIP_BOX + Ra-01) → LoRa по ID игрока (0 = все).
Выброс — в минутах. Радио — только трек. Громкость — кнопки на ПДА.
"""

import time
import tkinter as tk
from tkinter import ttk, messagebox

from db.event_db import EventDB
from shared.master_channel import id_choices, player_id_from_target
from theme import module_header, tk_text_opts

KIND_LABELS = {
    "info": "Сообщение",
    "warning": "Предупреждение",
    "emission": "Выброс",
    "radio": "Радиоэфир",
}


class BroadcastFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, serial=None, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.serial = serial
        self.on_back = on_back
        self._build()
        self.refresh()

    def _build(self):
        module_header(self, "Общие оповещения", self.on_back)

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        left = ttk.LabelFrame(body, text="Новое оповещение")
        left.pack(side="left", fill="y", padx=(0, 10))

        ttk.Label(left, text="Тип").pack(anchor="w", padx=8, pady=(8, 2))
        self.var_kind = tk.StringVar(value="info")
        ttk.Combobox(left, textvariable=self.var_kind, state="readonly", width=22,
                     values=list(KIND_LABELS.keys())).pack(padx=8, pady=(0, 6))

        ttk.Label(left, text="Кому (ID игрока, 0 = все)").pack(
            anchor="w", padx=8, pady=(4, 2))
        self.var_target = tk.StringVar(value="0")
        self.cmb_target = ttk.Combobox(left, textvariable=self.var_target, width=22)
        self.cmb_target.pack(padx=8, pady=(0, 6))

        ttk.Label(left, text="Текст (до 48 символов в LoRa)").pack(
            anchor="w", padx=8, pady=(4, 2))
        self.txt = tk.Text(left, width=36, height=6, **tk_text_opts())
        self.txt.pack(padx=8, pady=(0, 6))

        em = ttk.Frame(left)
        em.pack(fill="x", padx=8, pady=4)
        ttk.Label(em, text="Выброс: таймер").pack(side="left")
        self.var_timer = tk.StringVar(value="30")
        ttk.Entry(em, textvariable=self.var_timer, width=5).pack(side="left", padx=4)
        ttk.Label(em, text="мин, длит.").pack(side="left")
        self.var_dur = tk.StringVar(value="5")
        ttk.Entry(em, textvariable=self.var_dur, width=5).pack(side="left", padx=4)
        ttk.Label(em, text="мин").pack(side="left")

        rd = ttk.Frame(left)
        rd.pack(fill="x", padx=8, pady=4)
        ttk.Label(rd, text="Радио: трек").pack(side="left")
        self.var_track = tk.StringVar(value="1")
        ttk.Entry(rd, textvariable=self.var_track, width=5).pack(side="left", padx=4)

        ttk.Button(left, text="В очередь (только база)",
                   command=self._queue_only).pack(fill="x", padx=8, pady=(10, 2))
        ttk.Button(left, text="Отправить по LoRa", style="Accent.TButton",
                   command=self._send_lora).pack(fill="x", padx=8, pady=2)

        ttk.Label(
            left,
            text="Адрес LoRa — только ID игрока события.\n"
                 "Имена в эфир не ходят.\n"
                 "Громкость — страница НАСТРОЙКИ на ПДА.\n"
                 "ERROR:NO_LORA — на столе нет Ra-01.",
            style="Dim.TLabel", justify="left",
        ).pack(anchor="w", padx=8, pady=(12, 8))

        right = ttk.LabelFrame(body, text="Журнал оповещений события")
        right.pack(side="left", fill="both", expand=True)
        cols = ("time", "kind", "target", "status", "text")
        self.tree = ttk.Treeview(right, columns=cols, show="headings", height=18)
        headers = {"time": "Время", "kind": "Тип", "target": "ID",
                   "status": "Статус", "text": "Текст"}
        widths = {"time": 120, "kind": 90, "target": 70, "status": 80, "text": 280}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w")
        self.tree.pack(fill="both", expand=True, padx=6, pady=6)

    def refresh(self):
        self.cmb_target["values"] = id_choices(self.db.list_players(self.event_id))
        for i in self.tree.get_children():
            self.tree.delete(i)
        for b in self.db.list_broadcasts(self.event_id):
            t = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(b.created_at))
            self.tree.insert("", "end", values=(
                t, KIND_LABELS.get(b.kind, b.kind), b.target, b.status, b.text,
            ))

    def _text(self):
        return self.txt.get("1.0", "end").strip()

    def _pid(self):
        return player_id_from_target(self.var_target.get())

    def _queue_only(self):
        text = self._text()
        if not text:
            messagebox.showwarning("Оповещение", "Введите текст", parent=self)
            return
        self.db.add_broadcast(
            self.event_id, text,
            kind=self.var_kind.get(),
            target=str(self._pid()),
            status="queued",
        )
        self.txt.delete("1.0", "end")
        self.refresh()

    def _send_lora(self):
        kind = self.var_kind.get()
        text = self._text()
        pid = self._pid()
        cmd_ok, resp = False, "нет serial"

        if kind == "emission":
            try:
                timer = int(self.var_timer.get())
                dur = int(self.var_dur.get())
            except ValueError:
                messagebox.showwarning("Выброс", "Таймер и длительность — целые минуты",
                                       parent=self)
                return
            if timer < 0 or dur < 0:
                messagebox.showwarning("Выброс", "Минуты не могут быть отрицательными",
                                       parent=self)
                return
            text = text or f"ВЫБРОС через {timer} мин, {dur} мин"
            if self.serial:
                cmd_ok, resp = self.serial.emission(timer, dur, pid)
        elif kind == "radio":
            try:
                track = int(self.var_track.get())
            except ValueError:
                messagebox.showwarning("Радио", "Трек — целое число", parent=self)
                return
            text = text or f"RADIO track={track}"
            if self.serial:
                cmd_ok, resp = self.serial.radio(track, pid)
        else:
            if not text:
                messagebox.showwarning("Оповещение", "Введите текст", parent=self)
                return
            if self.serial:
                cmd_ok, resp = self.serial.broadcast(text, pid)

        status = "sent" if cmd_ok else "failed"
        self.db.add_broadcast(
            self.event_id, text,
            kind=kind,
            target=str(pid),
            status=status,
            serial_response=str(resp or ""),
        )
        self.refresh()
        if cmd_ok:
            messagebox.showinfo("Оповещение", f"LoRa на ID {pid or 'все'}.\n{resp}",
                                parent=self)
        else:
            messagebox.showwarning(
                "Оповещение",
                "Сохранено в журнале.\n"
                f"{resp}\n"
                "Полевая доставка — LoRa со стола мастера (CHIP_BOX + Ra-01).",
                parent=self,
            )
