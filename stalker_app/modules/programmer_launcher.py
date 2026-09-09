"""
STALKER App — запуск модуля «Программатор»
==========================================
programmer.py остаётся pygame-процессом. Перед запуском приложение
освобождает COM (shared/serial_session.py).
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


def launch_programmer(serial_session=None):
    """Запустить programmer.py. Возвращает (ok: bool, message: str)."""
    global _current_process
    if is_running():
        return False, "Программатор уже запущен"
    if not os.path.isfile(PROGRAMMER_SCRIPT):
        return False, f"Не найден {PROGRAMMER_SCRIPT}"
    if serial_session is not None:
        serial_session.release_for_programmer()
    try:
        _current_process = subprocess.Popen(
            [sys.executable, PROGRAMMER_SCRIPT],
            cwd=PROGRAMMER_DIR,
        )
    except Exception as exc:
        if serial_session is not None:
            serial_session.programmer_closed()
        return False, f"Не удалось запустить: {exc}"
    return True, "Программатор запущен в отдельном окне (чипы, аномалии, убежища, ПДА, терминалы)"


def poll_programmer(serial_session=None):
    """Если процесс программатора завершился — вернуть COM приложению."""
    global _current_process
    if _current_process is None:
        return
    if _current_process.poll() is not None:
        _current_process = None
        if serial_session is not None:
            serial_session.programmer_closed()
