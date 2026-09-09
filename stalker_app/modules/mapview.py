"""Карта локации: скриншот-подложка и маяки с известными координатами."""

import os
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog

from db.event_db import EventDB, BEACON_KINDS
from theme import module_header, ACCENT, ACCENT2, DANGER, CYAN, FG_DIM

KIND_LABEL = {
    "uwb": "UWB якорь",
    "shelter": "Убежище",
    "anomaly": "Аномалия",
    "checkpoint": "КПП",
    "other": "Другое",
}
KIND_COLOR = {
    "uwb": CYAN,
    "shelter": ACCENT2,
    "anomaly": DANGER,
    "checkpoint": ACCENT,
    "other": FG_DIM,
}


def _load_photo(path, max_w, max_h):
    """Tk PhotoImage; JPEG/WebP через Pillow, иначе PNG/GIF."""
    if not path or not os.path.isfile(path):
        return None, None
    ext = os.path.splitext(path)[1].lower()
    try:
        from PIL import Image, ImageTk
        img = Image.open(path)
        try:
            resample = Image.Resampling.LANCZOS
        except AttributeError:
            resample = Image.LANCZOS
        img.thumbnail((max(1, max_w), max(1, max_h)), resample)
        return ImageTk.PhotoImage(img), img.size
    except Exception:
        if ext not in (".png", ".gif"):
            return None, None
        try:
            photo = tk.PhotoImage(file=path)
            return photo, (photo.width(), photo.height())
        except tk.TclError:
            return None, None


class MapFrame(ttk.Frame):
    def __init__(self, master, db: EventDB = None, event_id: str = None,
                 on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.on_back = on_back
        self._photo = None
        self._img_box = (0, 0, 1, 1)  # x0,y0,w,h on canvas
        self._place_kind = tk.StringVar(value="uwb")
        self._build()
        self.after(80, self.redraw)

    def _build(self):
        module_header(self, "Карта локации", self.on_back)
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 10))

        tools = ttk.Frame(body)
        tools.pack(fill="x", pady=(0, 6))
        ttk.Button(tools, text="Загрузить скриншот…", style="Accent.TButton",
                   command=self._load_image).pack(side="left")
        ttk.Button(tools, text="Убрать подложку",
                   command=self._clear_image).pack(side="left", padx=6)
        ttk.Label(tools, text="Новый маяк:").pack(side="left", padx=(16, 4))
        ttk.Combobox(
            tools, textvariable=self._place_kind, state="readonly", width=14,
            values=list(KIND_LABEL.keys()),
        ).pack(side="left")
        ttk.Label(
            tools, text="Клик по карте — поставить. Координаты заранее известны.",
            style="Dim.TLabel",
        ).pack(side="left", padx=10)

        mid = ttk.Frame(body)
        mid.pack(fill="both", expand=True)

        self.canvas = tk.Canvas(
            mid, bg="#1a1c22", highlightthickness=1, highlightbackground="#3a3d4a",
        )
        self.canvas.pack(side="left", fill="both", expand=True)
        self.canvas.bind("<Button-1>", self._on_click)
        self.canvas.bind("<Configure>", lambda _e: self.redraw())

        right = ttk.LabelFrame(mid, text="Маяки")
        right.pack(side="left", fill="y", padx=(10, 0))
        cols = ("name", "kind", "xy")
        self.tree = ttk.Treeview(right, columns=cols, show="headings", height=16)
        self.tree.heading("name", text="Имя")
        self.tree.heading("kind", text="Тип")
        self.tree.heading("xy", text="%")
        self.tree.column("name", width=120)
        self.tree.column("kind", width=90)
        self.tree.column("xy", width=80)
        self.tree.pack(fill="both", expand=True, padx=6, pady=6)
        ttk.Button(right, text="Удалить выбранный", style="Danger.TButton",
                   command=self._delete_selected).pack(fill="x", padx=6, pady=(0, 8))

    def _image_path(self):
        if not self.db:
            return None
        meta = self.db.get_map_meta(self.event_id)
        if not meta:
            return None
        return meta["image_path"]

    def redraw(self):
        self.canvas.delete("all")
        w = max(self.canvas.winfo_width(), 40)
        h = max(self.canvas.winfo_height(), 40)
        path = self._image_path()
        photo, size = _load_photo(path, w - 8, h - 8) if path else (None, None)
        self._photo = photo
        if photo and size:
            iw, ih = size
            x0 = (w - iw) // 2
            y0 = (h - ih) // 2
            self._img_box = (x0, y0, iw, ih)
            self.canvas.create_image(x0, y0, anchor="nw", image=photo)
        else:
            self._img_box = (8, 8, w - 16, h - 16)
            self.canvas.create_rectangle(
                *self._img_box, outline="#3a3d4a", dash=(4, 3)
            )
            self.canvas.create_text(
                w // 2, h // 2, fill="#8b8d9a",
                text="Загрузите скриншот местности\nи расставьте маяки кликом",
                font=("DejaVu Sans", 12), justify="center",
            )

        if not self.db:
            return
        for i in self.tree.get_children():
            self.tree.delete(i)
        x0, y0, iw, ih = self._img_box
        for b in self.db.list_beacons(self.event_id):
            px = x0 + iw * (b.x_pct / 100.0)
            py = y0 + ih * (b.y_pct / 100.0)
            color = KIND_COLOR.get(b.kind, FG_DIM)
            r = 7
            self.canvas.create_oval(px - r, py - r, px + r, py + r,
                                    outline=color, width=2, fill="#16161c")
            self.canvas.create_text(
                px + 10, py - 10, anchor="w", fill=color, text=b.name,
                font=("DejaVu Sans", 9),
            )
            self.tree.insert(
                "", "end", iid=str(b.beacon_id),
                values=(b.name, KIND_LABEL.get(b.kind, b.kind),
                        f"{b.x_pct:.1f},{b.y_pct:.1f}"),
            )

    def _pct_from_event(self, event):
        x0, y0, iw, ih = self._img_box
        if iw <= 0 or ih <= 0:
            return None
        if not (x0 <= event.x <= x0 + iw and y0 <= event.y <= y0 + ih):
            return None
        x_pct = (event.x - x0) / iw * 100.0
        y_pct = (event.y - y0) / ih * 100.0
        return x_pct, y_pct

    def _on_click(self, event):
        if not self.db:
            return
        xy = self._pct_from_event(event)
        if not xy:
            return
        kind = self._place_kind.get() or "uwb"
        name = simpledialog.askstring(
            "Маяк", "Название (известная точка):",
            initialvalue=KIND_LABEL.get(kind, "маяк"),
            parent=self,
        )
        if not name:
            return
        self.db.add_beacon(self.event_id, name, kind, xy[0], xy[1])
        self.redraw()

    def _load_image(self):
        if not self.db:
            return
        path = filedialog.askopenfilename(
            title="Скриншот местности",
            filetypes=[
                ("Изображения", "*.png *.jpg *.jpeg *.gif *.webp"),
                ("Все", "*.*"),
            ],
            parent=self,
        )
        if not path:
            return
        dest = self.db.set_map_image(self.event_id, path)
        self.redraw()
        messagebox.showinfo("Карта", f"Подложка:\n{dest}", parent=self)

    def _clear_image(self):
        if not self.db:
            return
        self.db.clear_map_image(self.event_id)
        self.redraw()

    def _delete_selected(self):
        sel = self.tree.selection()
        if not sel:
            return
        self.db.delete_beacon(int(sel[0]))
        self.redraw()
