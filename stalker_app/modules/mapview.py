"""Карта локации — заглушка с местом под схему полигона (PROGRESSION §11)."""

import tkinter as tk
from tkinter import ttk

from theme import module_header


class MapFrame(ttk.Frame):
    def __init__(self, master, on_back=None):
        super().__init__(master)
        module_header(self, "Карта локации", on_back)
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=20, pady=20)
        ttk.Label(body, text="Схема полигона", style="Accent.TLabel").pack(
            anchor="w", pady=(0, 8))
        ttk.Label(
            body,
            text="Для мастеров на ноутбуке: якоря UWB, зоны безопасности, аномалии.\n"
                 "Калибровка BU03 — AT+SETDEV (ROADMAP фаза 5.5).\n"
                 "Отдельная спецификация — после стабилизации прошивок поля.\n\n"
                 "Сейчас модуль держит место в меню: карта не рисуется, "
                 "чтобы не путать с боевым трекингом.",
            style="Dim.TLabel", justify="left", wraplength=640,
        ).pack(anchor="w")
        canvas = tk.Canvas(body, bg="#1a1c22", highlightthickness=1,
                           highlightbackground="#3a3d4a", height=280)
        canvas.pack(fill="both", expand=True, pady=(16, 0))
        canvas.create_text(
            20, 20, anchor="nw", fill="#8b8d9a",
            text="[ ] якоря UWB    [ ] убежища    [ ] аномалии    [ ] КПП",
            font=("DejaVu Sans", 11),
        )
        canvas.create_rectangle(40, 70, 180, 160, outline="#4ad07a", dash=(4, 3))
        canvas.create_text(110, 110, fill="#4ad07a", text="ЗЗ")
        canvas.create_oval(240, 80, 300, 140, outline="#e05555")
        canvas.create_text(270, 110, fill="#e05555", text="Аном.")
        canvas.create_rectangle(360, 90, 420, 140, outline="#e8a317")
        canvas.create_text(390, 115, fill="#e8a317", text="КПП")
