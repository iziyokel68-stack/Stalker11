"""Каталог квестов / доска заданий (PROGRESSION.txt §9.4, ROADMAP §4.4)."""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from db.event_db import EventDB
from theme import module_header, tk_text_opts

CLAIM_MODES = ("timeout", "oneshot", "shared")
CLAIM_LABELS = {
    "timeout": "Таймаут (снова висит)",
    "oneshot": "Один раз",
    "shared": "Для всех",
}


class QuestsFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, serial=None, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.serial = serial
        self.on_back = on_back
        self.selected_id = None
        self._build()
        self.refresh()

    def _build(self):
        module_header(self, "Доска заданий", self.on_back)
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        left = ttk.LabelFrame(body, text="Каталог события")
        left.pack(side="left", fill="both", expand=True)
        cols = ("code", "title", "rub", "mode", "hidden")
        self.tree = ttk.Treeview(left, columns=cols, show="headings", height=18)
        headers = {
            "code": "ID", "title": "Название", "rub": "RUB",
            "mode": "Выдача", "hidden": "Скрытый",
        }
        widths = {"code": 70, "title": 200, "rub": 60, "mode": 90, "hidden": 70}
        for c in cols:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w")
        self.tree.pack(fill="both", expand=True, padx=6, pady=6)
        self.tree.bind("<<TreeviewSelect>>", self._on_select)

        form = ttk.LabelFrame(body, text="Карточка задания")
        form.pack(side="left", fill="y", padx=(10, 0))
        self.var_code = tk.StringVar()
        self.var_title = tk.StringVar()
        self.var_rub = tk.StringVar(value="300")
        self.var_hidden = tk.BooleanVar(value=False)
        self.var_mode = tk.StringVar(value="timeout")
        self.var_timeout = tk.StringVar(value="120")

        def row(label, var, width=28):
            r = ttk.Frame(form)
            r.pack(fill="x", padx=8, pady=4)
            ttk.Label(r, text=label, width=14).pack(side="left")
            ttk.Entry(r, textvariable=var, width=width).pack(side="left")

        row("Код *", self.var_code)
        row("Название *", self.var_title)
        row("Награда RUB", self.var_rub)

        mr = ttk.Frame(form)
        mr.pack(fill="x", padx=8, pady=4)
        ttk.Label(mr, text="Выдача", width=14).pack(side="left")
        self.cmb_mode = ttk.Combobox(
            mr, state="readonly", width=24,
            values=[CLAIM_LABELS[m] for m in CLAIM_MODES],
        )
        self.cmb_mode.current(0)
        self.cmb_mode.pack(side="left")
        self.cmb_mode.bind("<<ComboboxSelected>>", self._on_mode)

        row("Таймаут, мин", self.var_timeout, width=10)
        ttk.Checkbutton(form, text="Скрытый (ур. 20+)",
                        variable=self.var_hidden).pack(anchor="w", padx=8, pady=4)
        ttk.Label(form, text="Полный текст (на доске / ПК)").pack(
            anchor="w", padx=8, pady=(6, 2))
        self.txt_body = tk.Text(form, width=32, height=8, **tk_text_opts())
        self.txt_body.pack(padx=8, pady=(0, 8))

        ttk.Button(form, text="Добавить", command=self._add).pack(fill="x", padx=8, pady=2)
        ttk.Button(form, text="Сохранить", command=self._save).pack(fill="x", padx=8, pady=2)
        ttk.Button(form, text="Удалить", style="Danger.TButton",
                   command=self._delete).pack(fill="x", padx=8, pady=2)
        ttk.Button(form, text="Прошить доску (USB)", style="Accent.TButton",
                   command=self._flash).pack(fill="x", padx=8, pady=(10, 2))
        ttk.Button(form, text="Экспорт CSV", command=self._export).pack(
            fill="x", padx=8, pady=2)
        ttk.Label(
            form,
            text="Таймаут — эксклюзивно, снова висит.\n"
                 "Один раз — взяли и больше не появляется.\n"
                 "Для всех — висит постоянно.\n"
                 "На ПДА уходит ID + название.\n"
                 "XP за сдачу = RUB награды.",
            style="Dim.TLabel", justify="left",
        ).pack(anchor="w", padx=8, pady=(10, 8))

    def _mode_from_ui(self) -> str:
        label = (self.cmb_mode.get() or "").strip()
        for key, text in CLAIM_LABELS.items():
            if text == label:
                return key
        return "timeout"

    def _on_mode(self, _e=None):
        self.var_mode.set(self._mode_from_ui())

    def refresh(self):
        for i in self.tree.get_children():
            self.tree.delete(i)
        for q in self.db.list_quests(self.event_id):
            mode = CLAIM_LABELS.get(q.claim_mode, q.claim_mode)
            self.tree.insert("", "end", iid=str(q.quest_id), values=(
                q.code, q.title, q.reward_rub, mode, "да" if q.hidden else "",
            ))

    def _on_select(self, _e=None):
        sel = self.tree.selection()
        if not sel:
            return
        q = self.db.get_quest(int(sel[0]))
        if not q:
            return
        self.selected_id = q.quest_id
        self.var_code.set(q.code)
        self.var_title.set(q.title)
        self.var_rub.set(str(q.reward_rub))
        self.var_hidden.set(bool(q.hidden))
        self.var_mode.set(q.claim_mode or "timeout")
        self.var_timeout.set(str(q.timeout_min or 120))
        idx = CLAIM_MODES.index(q.claim_mode) if q.claim_mode in CLAIM_MODES else 0
        self.cmb_mode.current(idx)
        self.txt_body.delete("1.0", "end")
        self.txt_body.insert("1.0", q.body or "")

    def _fields(self):
        code = self.var_code.get().strip()
        title = self.var_title.get().strip()
        if not code or not title:
            messagebox.showwarning("Квест", "Нужны код и название", parent=self)
            return None
        try:
            rub = int(self.var_rub.get() or 0)
        except ValueError:
            messagebox.showwarning("Квест", "Награда — целое число", parent=self)
            return None
        try:
            tmin = int(self.var_timeout.get() or 120)
        except ValueError:
            messagebox.showwarning("Квест", "Таймаут — целое число минут", parent=self)
            return None
        if tmin <= 0:
            tmin = 120
        return {
            "code": code,
            "title": title,
            "body": self.txt_body.get("1.0", "end").strip(),
            "reward_rub": rub,
            "hidden": self.var_hidden.get(),
            "claim_mode": self._mode_from_ui(),
            "timeout_min": tmin,
        }

    def _add(self):
        fields = self._fields()
        if not fields:
            return
        self.db.add_quest(self.event_id, **fields)
        self.refresh()

    def _save(self):
        if self.selected_id is None:
            messagebox.showinfo("Квест", "Выберите задание", parent=self)
            return
        fields = self._fields()
        if not fields:
            return
        fields["hidden"] = 1 if fields["hidden"] else 0
        self.db.update_quest(self.selected_id, **fields)
        self.refresh()

    def _delete(self):
        if self.selected_id is None:
            return
        if not messagebox.askyesno("Квест", "Удалить задание из каталога?", parent=self):
            return
        self.db.delete_quest(self.selected_id)
        self.selected_id = None
        self.refresh()

    def _flash(self):
        if self.serial is None:
            messagebox.showwarning(
                "Доска", "USB не подключён в этом окне", parent=self)
            return
        quests = self.db.list_quests(self.event_id)
        if not quests:
            messagebox.showwarning("Доска", "Каталог пуст", parent=self)
            return
        if not messagebox.askyesno(
            "Доска",
            "Прошить все задания на подключённый терминал QUEST?\n"
            "Кассета EEPROM должна быть в доске.",
            parent=self,
        ):
            return
        ok, msg = self.serial.flash_quest_board(quests)
        if ok:
            messagebox.showinfo("Доска", f"Каталог записан.\n{msg}", parent=self)
        else:
            messagebox.showerror("Доска", msg, parent=self)

    def _export(self):
        path = filedialog.asksaveasfilename(
            title="Экспорт каталога квестов",
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv")],
            initialfile="quests.csv",
            parent=self,
        )
        if not path:
            return
        self.db.export_quests_csv(self.event_id, path)
        messagebox.showinfo("Экспорт", f"Сохранено: {path}", parent=self)
