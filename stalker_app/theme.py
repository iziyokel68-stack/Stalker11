"""Тёмная тема консоли мастера STALKER (Tkinter/ttk).

Контраст: тёплый светлый текст на почти чёрном фоне, подписи — песочные,
не серый-на-сером.
"""

import sys
import tkinter as tk
from tkinter import ttk

BG = "#0a0a0c"
BG_PANEL = "#14120e"
BG_RAISED = "#2a2418"
BG_INPUT = "#1a1814"
FG = "#fff6e0"
FG_DIM = "#d4b56a"
FG_MUTED = "#a8884a"
ACCENT = "#ffb020"
ACCENT2 = "#5ee08a"
DANGER = "#ff5a4a"
CYAN = "#6ed4ff"
BORDER = "#8a7038"

FONT_UI = ("Segoe UI", 10) if sys.platform == "win32" else ("DejaVu Sans", 10)
FONT_UI_B = (FONT_UI[0], 10, "bold")
FONT_H1 = (FONT_UI[0], 20, "bold")
FONT_H2 = (FONT_UI[0], 13, "bold")
FONT_SMALL = (FONT_UI[0], 9)
FONT_MONO = ("Consolas", 10) if sys.platform == "win32" else ("DejaVu Sans Mono", 10)


def tk_text_opts(**extra):
    """Контрастные цвета для tk.Text."""
    opts = dict(
        bg=BG_INPUT, fg=FG, insertbackground=FG, borderwidth=0,
        highlightthickness=1, highlightbackground=BORDER,
        selectbackground="#6a4a10", selectforeground="#fff4c8",
    )
    opts.update(extra)
    return opts


def tk_list_opts(**extra):
    """Контрастные цвета для tk.Listbox (без insertbackground)."""
    opts = dict(
        bg=BG_INPUT, fg=FG, borderwidth=0, highlightthickness=0,
        highlightbackground=BORDER,
        selectbackground="#6a4a10", selectforeground="#fff4c8",
    )
    opts.update(extra)
    return opts


def apply_theme(root: tk.Tk):
    root.configure(bg=BG)
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass

    style.configure(".", background=BG, foreground=FG, fieldbackground=BG_INPUT,
                    bordercolor=BORDER, font=FONT_UI, lightcolor=BORDER,
                    darkcolor=BORDER)
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
              background=[("active", "#3d3420"), ("pressed", "#1c1810")],
              foreground=[("disabled", FG_MUTED)])
    style.configure("Accent.TButton", background="#6a4a10", foreground="#ffe7a0")
    style.map("Accent.TButton", background=[("active", "#8a6214")])
    style.configure("Danger.TButton", background="#5a1818", foreground="#ffc8c0")
    style.map("Danger.TButton", background=[("active", "#7a2222")])

    style.configure("Nav.TButton", background=BG_PANEL, foreground=FG, anchor="w",
                    padding=(14, 10), font=FONT_UI)
    style.map("Nav.TButton", background=[("active", BG_RAISED)])
    style.configure("NavSel.TButton", background="#5a3c0c", foreground=ACCENT,
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
    style.configure("TNotebook", background=BG, bordercolor=BORDER)
    style.configure("TNotebook.Tab", background=BG_RAISED, foreground=FG,
                    padding=(12, 6), font=FONT_UI)
    style.map("TNotebook.Tab", background=[("selected", "#5a3c0c")],
              foreground=[("selected", ACCENT)])

    style.configure("TScale", background=BG, troughcolor=BG_RAISED)

    style.configure("Treeview", background=BG_INPUT, foreground=FG,
                    fieldbackground=BG_INPUT, bordercolor=BORDER, rowheight=24,
                    font=FONT_UI)
    style.configure("Treeview.Heading", background=BG_RAISED, foreground=ACCENT,
                    font=FONT_UI_B, bordercolor=BORDER)
    style.map("Treeview",
              background=[("selected", "#6a4a10")],
              foreground=[("selected", "#fff4c8")])

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
