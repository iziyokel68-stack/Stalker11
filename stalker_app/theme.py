"""Тёмная тема консоли мастера STALKER (Tkinter/ttk)."""

import sys
import tkinter as tk
from tkinter import ttk

BG = "#16161c"
BG_PANEL = "#1e1f27"
BG_RAISED = "#2a2c36"
BG_INPUT = "#252732"
FG = "#e6e6ea"
FG_DIM = "#8b8d9a"
FG_MUTED = "#6a6c78"
ACCENT = "#e8a317"
ACCENT2 = "#4ad07a"
DANGER = "#e05555"
CYAN = "#5ec8ff"
BORDER = "#3a3d4a"

FONT_UI = ("Segoe UI", 10) if sys.platform == "win32" else ("DejaVu Sans", 10)
FONT_UI_B = (FONT_UI[0], 10, "bold")
FONT_H1 = (FONT_UI[0], 20, "bold")
FONT_H2 = (FONT_UI[0], 13, "bold")
FONT_SMALL = (FONT_UI[0], 9)
FONT_MONO = ("Consolas", 10) if sys.platform == "win32" else ("DejaVu Sans Mono", 10)


def apply_theme(root: tk.Tk):
    root.configure(bg=BG)
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass

    style.configure(".", background=BG, foreground=FG, fieldbackground=BG_INPUT,
                    bordercolor=BORDER, font=FONT_UI)
    style.configure("TFrame", background=BG)
    style.configure("Panel.TFrame", background=BG_PANEL)
    style.configure("TLabel", background=BG, foreground=FG, font=FONT_UI)
    style.configure("Dim.TLabel", background=BG, foreground=FG_DIM, font=FONT_SMALL)
    style.configure("Muted.TLabel", background=BG, foreground=FG_MUTED)
    style.configure("Accent.TLabel", background=BG, foreground=ACCENT, font=FONT_H2)
    style.configure("Title.TLabel", background=BG, foreground=ACCENT, font=FONT_H1)
    style.configure("Ok.TLabel", background=BG, foreground=ACCENT2)
    style.configure("Err.TLabel", background=BG, foreground=DANGER)
    style.configure("TLabelframe", background=BG, foreground=FG, bordercolor=BORDER)
    style.configure("TLabelframe.Label", background=BG, foreground=ACCENT, font=FONT_UI_B)

    style.configure("TButton", background=BG_RAISED, foreground=FG, bordercolor=BORDER,
                    focusthickness=0, padding=(10, 6), font=FONT_UI)
    style.map("TButton",
              background=[("active", "#3a3d4c"), ("pressed", "#22232c")],
              foreground=[("disabled", FG_MUTED)])
    style.configure("Accent.TButton", background="#5a3d0e", foreground=ACCENT)
    style.map("Accent.TButton", background=[("active", "#7a5414")])
    style.configure("Danger.TButton", background="#4a2020", foreground=DANGER)
    style.map("Danger.TButton", background=[("active", "#6a2a2a")])

    style.configure("Nav.TButton", background=BG_PANEL, foreground=FG, anchor="w",
                    padding=(14, 10), font=FONT_UI)
    style.map("Nav.TButton", background=[("active", BG_RAISED)])
    style.configure("NavSel.TButton", background="#3d2e10", foreground=ACCENT,
                    anchor="w", padding=(14, 10), font=FONT_UI_B)

    style.configure("TEntry", fieldbackground=BG_INPUT, foreground=FG,
                    insertcolor=FG, bordercolor=BORDER)
    style.configure("TCombobox", fieldbackground=BG_INPUT, foreground=FG,
                    background=BG_RAISED, bordercolor=BORDER)
    style.map("TCombobox", fieldbackground=[("readonly", BG_INPUT)],
              foreground=[("readonly", FG)])

    style.configure("TCheckbutton", background=BG, foreground=FG)
    style.configure("TRadiobutton", background=BG, foreground=FG)
    style.configure("TSeparator", background=BORDER)
    style.configure("TScrollbar", background=BG_RAISED, troughcolor=BG,
                    bordercolor=BORDER)

    style.configure("Treeview", background=BG_INPUT, foreground=FG,
                    fieldbackground=BG_INPUT, bordercolor=BORDER, rowheight=24,
                    font=FONT_UI)
    style.configure("Treeview.Heading", background=BG_RAISED, foreground=ACCENT,
                    font=FONT_UI_B, bordercolor=BORDER)
    style.map("Treeview",
              background=[("selected", "#4a3710")],
              foreground=[("selected", ACCENT)])

    style.configure("Status.TFrame", background=BG_PANEL)
    style.configure("Status.TLabel", background=BG_PANEL, foreground=FG_DIM,
                    font=FONT_MONO)


def module_header(parent, title: str, on_back=None):
    top = ttk.Frame(parent)
    top.pack(fill="x", padx=12, pady=(10, 6))
    ttk.Label(top, text=title, style="Accent.TLabel").pack(side="left")
    if on_back:
        ttk.Button(top, text="← Меню", command=on_back).pack(side="right")
    return top
