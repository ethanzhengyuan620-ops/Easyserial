# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project Overview

**Easy串口助手** (Easy Serial Assistant) is a PyQt6-based serial port debugging tool with dark theme, multiple display modes (ASCII/HEX), and advanced text coloring features.

## Development Commands

### Run the application
```bash
python main.py
```

### Install dependencies
```bash
pip install -r requirements.txt
```

### Build single-file Windows executable
```bash
build.bat
```
Outputs to `dist\EasySerial.exe`

## Architecture

### Three-Layer Design

#### 1. UI Layer (`ui/`)
PyQt6-based GUI components:
- **main_window.py**: Main window orchestration, layout (left config panel + right receive/send areas), menu/toolbar setup
- **serial_panel.py**: Port/baud/parity configuration controls
- **receive_view.py**: Data receive area with multiple display modes (ASCII, HEX-dual, HEX-pure)
- **send_panel.py**: Sending area with format (ASCII/HEX), line endings, timer send, and 6 customizable quick commands
- **search_bar.py**: Floating search UI for receive view
- **rule_editor.py**: Dialog to edit text coloring rules
- **tools_dialog.py**: Timestamp converter and other utilities
- **appearance.py**: Font and color customization dialog
- **theme.py**: Centralized dark theme CSS (QSS)
- **widgets.py**: Custom reusable UI components

#### 2. Core Layer (`core/`)
Data processing logic:
- **serial_worker.py**: QThread-based serial reader
  - Reads in a background thread, writes are thread-safe via `threading.Lock`
  - Emits signals: `frame_received(bytes, datetime)`, `opened()`, `error(str)`, `closed()`
  - Handles port open/close, baud rate, parity, stop bits, data bits
  - Passes incoming bytes to FrameParser for framing
  
- **frame_parser.py**: Byte-to-frame conversion
  - Splits on `\n` (0x0A) character
  - Max frame size: 4096 bytes
  - Timeout-based flush (500ms) for frames without newline
  - Returns `(frame_bytes, first_byte_datetime)` tuples
  
- **color_engine.py**: Text coloring system
  - **Tag-based coloring (Type B)**: Inline `[color:"text"]` tags sent from device (e.g., `[red:"ERROR"]`)
  - **Rule-based coloring (Type C)**: User-editable regex rules that match and colorize text (e.g., `ERROR|FAIL` → red)
  - Also contains HEX string parser utility shared by send area and tools dialog

#### 3. Config Layer (`config/`)
- **settings.py**: JSON-based configuration
  - Dual-path strategy: dev mode uses `config.json` at project root; packaged mode uses `%APPDATA%/SerialAssistant/config.json`
  - Automatic backup of defaults; invalid JSON gracefully falls back
  - Keys include: port, baud, display mode, font, colors, rules, quick commands, window geometry

## Key Data Flows

### Incoming Data
1. SerialWorker reads from serial port in background thread
2. Passes bytes to FrameParser, which buffers until newline or timeout
3. Emits `frame_received(frame_bytes, datetime)` signal
4. ReceiveView receives signal, applies color rules, inserts into QPlainTextEdit
5. Auto-scrolls if enabled; maintains max line count to prevent memory bloat

### Outgoing Data
1. SendPanel builds message from format (ASCII or HEX parsed)
2. Appends line ending (none/\r\n/\r/\n)
3. Calls SerialWorker.write() (thread-safe)
4. Optional timer send repeats at interval

### Color Application
- FrameParser provides first-byte timestamp (used for timestamp column)
- ColorEngine processes each frame's text through:
  1. Tag parsing for inline colors from device
  2. Rule matching for keyword-based colors
- ReceiveView formats and renders colored text

## Configuration Structure (config.json)

```json
{
  "port": "COM3",
  "baud": 115200,
  "mode": 1,              // 0=ASCII, 1=HEX-dual, 2=HEX-pure
  "font_family": "Consolas",
  "font_size": 10,
  "col_text": "#E2E8F0",
  "rules": [              // User-editable regex coloring rules
    {"pattern": "ERROR", "color": "#FF5252", "enabled": true}
  ],
  "quick_cmds": [         // 6 customizable quick send buttons
    {"name": "AT", "data": "AT", "hex": false}
  ]
}
```

## Important Notes

- **Threading**: All serial I/O is thread-safe. UI updates happen on main thread via Qt signals.
- **Memory management**: ReceiveView enforces `max_lines` setting (default 5000) with native QPlainTextEdit line culling to keep memory constant.
- **Font scaling**: Ctrl+scroll or A±  buttons adjust font size; font is attached to widget level so all text rescales instantly without reformatting.
- **HEX display**: Two modes—dual-column (left: bytes, right: single-char ASCII) with auto-wrap and line alignment; pure mode forces width-based line breaks for large datasets.
- **Window state**: Geometry and panel sizes are persisted via settings; pin button toggles always-on-top.
- **Config persistence**: Changes auto-save immediately (except in dev mode where JSON exists locally).

## Common Development Tasks

### Adding a new coloring rule type
1. Extend `color_engine.py` with new rule parsing logic
2. Update ReceiveView to call the new coloring function
3. Add rule editor UI if user-configurable

### Modifying display modes
- Core rendering is in ReceiveView's format methods
- Update color_engine.py if new formatting affects coloring

### Adding serial port parameters
- Add to DEFAULTS in settings.py
- Add UI control to SerialPanel
- Pass to SerialWorker constructor

### Building/distributing
- `build.bat` handles PyInstaller configuration
- Ensure all imports are covered (check hidden-import flags)
- Test the packaged EXE with all features before distribution
