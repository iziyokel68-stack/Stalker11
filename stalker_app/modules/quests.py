"""Каталог квестов / доска заданий (PROGRESSION.txt §9.4, ROADMAP §4.4)."""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from db.event_db import EventDB
from theme import module_header, tk_text_opts


class QuestsFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
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
        cols = ("code", "title", "rub", "hidden")
        self.tree = ttk.Treeview(left, columns=cols, show="headings", height=18)
        headers = {"code": "ID", "title": "Название", "rub": "RUB", "hidden": "Скрытый"}
        widths = {"code": 80, "title": 240, "rub": 70, "hidden": 80}
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

        def row(label, var, width=28):
            r = ttk.Frame(form)
            r.pack(fill="x", padx=8, pady=4)
            ttk.Label(r, text=label, width=12).pack(side="left")
            ttk.Entry(r, textvariable=var, width=width).pack(side="left")

        row("Код *", self.var_code)
        row("Название *", self.var_title)
        row("Награда RUB", self.var_rub)
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
        ttk.Button(form, text="Экспорт CSV", command=self._export).pack(
            fill="x", padx=8, pady=(10, 2))
        ttk.Label(
            form,
            text="В EEPROM ПДА пока уходит ID + имя.\n"
                 "Полный текст — для мастера на доске.\n"
                 "XP за сдачу = RUB награды.",
            style="Dim.TLabel", justify="left",
        ).pack(anchor="w", padx=8, pady=(10, 8))

    def refresh(self):
        for i in self.tree.get_children():
            self.tree.delete(i)
        for q in self.db.list_quests(self.event_id):
            self.tree.insert("", "end", iid=str(q.quest_id), values=(
                q.code, q.title, q.reward_rub, "да" if q.hidden else "",
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
        self.txt_body.delete("1.0", "end")
        self.txt_body.insert("1.0", q.body or "")

    def _add(self):
        code = self.var_code.get().strip()
        title = self.var_title.get().strip()
        if not code or not title:
            messagebox.showwarning("Квест", "Нужны код и название", parent=self)
            return
        try:
            rub = int(self.var_rub.get() or 0)
        except ValueError:
            messagebox.showwarning("Квест", "Награда — целое число", parent=self)
            return
        self.db.add_quest(
            self.event_id, code, title,
            body=self.txt_body.get("1.0", "end").strip(),
            reward_rub=rub,
            hidden=self.var_hidden.get(),
        )
        self.refresh()

    def _save(self):
        if self.selected_id is None:
            messagebox.showinfo("Квест", "Выберите задание", parent=self)
            return
        try:
            rub = int(self.var_rub.get() or 0)
        except ValueError:
            messagebox.showwarning("Квест", "Награда — целое число", parent=self)
            return
        self.db.update_quest(
            self.selected_id,
            code=self.var_code.get().strip(),
            title=self.var_title.get().strip(),
            body=self.txt_body.get("1.0", "end").strip(),
            reward_rub=rub,
            hidden=1 if self.var_hidden.get() else 0,
        )
        self.refresh()

    def _delete(self):
        if self.selected_id is None:
            return
        if not messagebox.askyesno("Квест", "Удалить задание из каталога?", parent=self):
            return
        self.db.delete_quest(self.selected_id)
        self.selected_id = None
        self.refresh()

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
