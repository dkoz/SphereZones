import threading
import webview
import os
import sys
from server import app


def start_server():
    app.run(host='127.0.0.1', port=8000, debug=False, use_reloader=False)


def get_resource_path(relative_path):
    if hasattr(sys, '_MEIPASS'):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath(os.path.dirname(__file__)), relative_path)


if __name__ == '__main__':
    server_thread = threading.Thread(target=start_server, daemon=True)
    server_thread.start()

    webview.create_window(
        'Sphere Zones',
        'http://127.0.0.1:8000',
        width=1280,
        height=800,
        min_size=(800, 600)
    )
    webview.start()
