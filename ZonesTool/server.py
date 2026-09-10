import os
import sys
import json
from flask import Flask, render_template, send_from_directory, request, jsonify

if getattr(sys, 'frozen', False):
    BASE_DIR = sys._MEIPASS
else:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))

app = Flask(__name__,
            template_folder=os.path.join(BASE_DIR, 'templates'),
            static_folder=os.path.join(BASE_DIR, 'static'))

TILES_DIR = os.path.join(BASE_DIR, 'tiles')

@app.route('/')
def index():
    return render_template('index.html')


@app.route('/tiles/<path:filepath>')
def serve_tiles(filepath):
    return send_from_directory(TILES_DIR, filepath)


@app.route('/api/save', methods=['POST'])
def save_file():
    data = request.json
    if not data:
        return jsonify({'error': 'No data'}), 400

    import webview
    result = webview.windows[0].create_file_dialog(
        webview.SAVE_DIALOG,
        save_filename='zones.json',
        file_types=('JSON Files (*.json)',)
    )

    if not result:
        return jsonify({'error': 'Cancelled'}), 400

    filepath = result if isinstance(result, str) else result[0]

    with open(filepath, 'w', encoding='utf-8') as f:
        json.dump(data['content'], f, indent=2)

    return jsonify({'success': True, 'path': filepath})


@app.route('/api/load', methods=['POST'])
def load_file():
    import webview
    result = webview.windows[0].create_file_dialog(
        webview.OPEN_DIALOG,
        file_types=('JSON Files (*.json)',)
    )

    if not result:
        return jsonify({'error': 'Cancelled'}), 400

    filepath = result if isinstance(result, str) else result[0]

    with open(filepath, 'r', encoding='utf-8') as f:
        content = json.load(f)

    return jsonify({'content': content})


if __name__ == '__main__':
    app.run(host='127.0.0.1', port=8000, debug=True)
