"""
STALKER App — устаревшие заглушки.
Глобальное сообщение → modules/broadcast.py
Карта → modules/mapview.py
"""

import tkinter as tk
from tkinter import ttk


class StubFrame(ttk.Frame):
    def __init__(self, master, title: str, note: str, on_back=None):
        super().__init__(master)
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=8)
        ttk.Label(top, text=title, font=("Segoe UI", 14, "bold")).pack(side="left")
        if on_back:
            ttk.Button(top, text="← Меню", command=on_back).pack(side="right")

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=20, pady=20)
        ttk.Label(body, text="В разработке", font=("Segoe UI", 20, "bold"),
                  foreground="#888").pack(pady=(40, 10))
        ttk.Label(body, text=note, justify="left", wraplength=420).pack()
