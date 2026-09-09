"""
STALKER App — запуск модуля «Программатор» (docs/PROGRESSION.txt §10.3 Фаза 1)
==================================================================================
programmer.py остаётся отдельным pygame-процессом без изменений — оболочка
только запускает его тем же интерпретатором и не блокирует главное меню.
"""

import os
import subprocess
import sys

PROGRAMMER_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc")
)
PROGRAMMER_SCRIPT = os.path.join(PROGRAMMER_DIR, "programmer.py")

_current_process = None


def is_running() -> bool:
    global _current_process
    return _current_process is not None and _current_process.poll() is None


def launch_programmer():
    """Запустить programmer.py как отдельный процесс (subprocess).

    Возвращает (ok: bool, message: str).
    """
    global _current_process
    if is_running():
        return False, "Программатор уже запущен"
    if not os.path.isfile(PROGRAMMER_SCRIPT):
        return False, f"Не найден {PROGRAMMER_SCRIPT}"
    try:
        _current_process = subprocess.Popen(
            [sys.executable, PROGRAMMER_SCRIPT],
            cwd=PROGRAMMER_DIR,
        )
    except Exception as exc:
        return False, f"Не удалось запустить: {exc}"
    return True, "Программатор запущен в отдельном окне"
