"""Команды мастера: LoRa по ID игрока события или EEPROM-чип (не USB к ПДА)."""

import tkinter as tk
from tkinter import ttk, messagebox

from shared.master_channel import id_choices, player_id_from_target
from theme import module_header, tk_text_opts


class CommandsFrame(ttk.Frame):
    def __init__(self, master, db, event_id, serial=None, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.serial = serial
        self.on_back = on_back
        self._build()

    def _build(self):
        module_header(self, "Команды мастера", self.on_back)
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        ttk.Label(
            body,
            text="ПДА игрока: LoRa на ID события или чип в CHIP_BOX. "
                 "Имена в эфир не ходят. USB — устройство мастера, не кабель к каждому ПДА.",
            style="Dim.TLabel", wraplength=820,
        ).pack(anchor="w", pady=(0, 10))

        row = ttk.Frame(body)
        row.pack(fill="x", pady=4)
        ttk.Label(row, text="Кому (ID, 0 = все)").pack(side="left")
        self.var_target = tk.StringVar(value="0")
        self.cmb_target = ttk.Combobox(row, textvariable=self.var_target, width=16)
        self.cmb_target.pack(side="left", padx=8)
        self._refresh_targets()

        row2 = ttk.Frame(body)
        row2.pack(fill="x", pady=8)
        ttk.Button(row2, text="Допуск LoRa", style="Accent.TButton",
                   command=self._admit_lora).pack(side="left", padx=4)
        ttk.Button(row2, text="Воскрешение LoRa",
                   command=self._revive_lora).pack(side="left", padx=4)
        ttk.Button(row2, text="KILL LoRa", style="Danger.TButton",
                   command=self._kill_lora).pack(side="left", padx=4)

        row3 = ttk.Frame(body)
        row3.pack(fill="x", pady=4)
        ttk.Button(row3, text="Прошить чип допуска",
                   command=self._chip_admit).pack(side="left", padx=4)
        ttk.Button(row3, text="Прошить чип воскрешения",
                   command=self._chip_revive).pack(side="left", padx=4)

        self.out = tk.Text(body, height=16, font=("DejaVu Sans Mono", 10),
                           **tk_text_opts())
        self.out.pack(fill="both", expand=True, pady=(10, 0))

        ttk.Label(
            body,
            text="ADMIT ≠ REVIVE. Допуск — каждое включение, главный мастер.\n"
                 "Воскрешение и KILL — по LoRa на ID игрока.",
            style="Dim.TLabel", justify="left",
        ).pack(anchor="w", pady=(8, 0))

    def _refresh_targets(self):
        self.cmb_target["values"] = id_choices(self.db.list_players(self.event_id))

    def _log(self, text):
        self.out.insert("end", text + "\n")
        self.out.see("end")

    def _pid(self):
        return player_id_from_target(self.var_target.get())

    def _admit_lora(self):
        if not self.serial:
            return
        pid = self._pid()
        ok, resp = self.serial.admit_lora(pid)
        self._log(("OK " if ok else "FAIL ") + f"ADMIT ID {pid}  {resp}")
        if not ok:
            messagebox.showerror("Допуск", str(resp), parent=self)

    def _revive_lora(self):
        if not self.serial:
            return
        pid = self._pid()
        ok, resp = self.serial.revive_lora(pid)
        self._log(("OK " if ok else "FAIL ") + f"REVIVE ID {pid}  {resp}")
        if not ok:
            messagebox.showerror("Воскрешение", str(resp), parent=self)

    def _kill_lora(self):
        if not self.serial:
            return
        pid = self._pid()
        ok, resp = self.serial.kill_lora(pid)
        self._log(("OK " if ok else "FAIL ") + f"KILL ID {pid}  {resp}")
        if not ok:
            messagebox.showerror("KILL", str(resp), parent=self)

    def _chip_admit(self):
        if not self.serial:
            return
        ok, resp = self.serial.write_admit_chip()
        self._log(("OK " if ok else "FAIL ") + "CHIP ADMIT  " + str(resp))

    def _chip_revive(self):
        if not self.serial:
            return
        ok, resp = self.serial.write_revive_chip()
        self._log(("OK " if ok else "FAIL ") + "CHIP REVIVE  " + str(resp))
