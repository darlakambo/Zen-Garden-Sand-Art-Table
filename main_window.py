"""
- application main window for Zen Garden Sand Art Table PC GUI
- Left panel with COM port selection, mode buttons, action buttons, and emergency stop
- Right panel with sand canvas preview and status bar
- Handles serial communication with the Arduino, updates canvas and status based on incoming messages, and sends commands based on user interactions
"""

import sys
import serial
import serial.tools.list_ports
from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QPushButton,
    QComboBox,
    QLabel,
    QFrame,
    QSizePolicy,
    QSpacerItem,
)
from PySide6.QtCore import Qt, QTimer, QThread, Signal
from PySide6.QtGui import QFont
from sand_canvas import SandCanvas


WINDOW_TITLE = "Zen Garden Sand Art Table"
WINDOW_MIN_W = 900
WINDOW_MIN_H = 600

#//////////////////////////////////////////////////////////////////////////////////////////////////////
# =============Stylesheet — earthy sand/wood palette, consistent font and spacing======================
#//////////////////////////////////////////////////////////////////////////////////////////////////////
STYLE_SHEET = """
QMainWindow, QWidget#root {
    background-color: #F7F1E8;
}

/* ---- Left panel ---- */
QWidget#leftPanel {
    background-color: #EDE5D5;
    border-right: 1px solid #D4C4A8;
}

/* ---- Section labels ---- */
QLabel#sectionLabel {
    font-family: 'Segoe UI', sans-serif;
    font-size: 9pt;
    font-weight: 600;
    color: #8B6914;
    letter-spacing: 1px;
    padding: 0px;
    text-transform: uppercase;
}

/* ---- Status bar labels ---- */
QLabel#statusLabel {
    font-family: 'Segoe UI', sans-serif;
    font-size: 9pt;
    color: #5A4520;
    background-color: #EDE5D5;
    border: 1px solid #D4C4A8;
    border-radius: 4px;
    padding: 4px 10px;
}

/* ---- All buttons base style ---- */
QPushButton {
    font-family: 'Segoe UI', sans-serif;
    font-size: 10pt;
    background-color: #E2D5BE;
    color: #3A2A10;
    border: 1px solid #C4B490;
    border-radius: 5px;
    padding: 7px 12px;
    min-height: 30px;
}
QPushButton:hover {
    background-color: #D5C8AE;
    border-color: #A89060;
}
QPushButton:pressed {
    background-color: #C4B49A;
}
QPushButton:disabled {
    background-color: #EDE8DD;
    color: #B8A888;
    border-color: #D8D0C0;
}

/* ---- Mode buttons: darken when active (checked) ---- */
QPushButton#modeBtn {
    text-align: left;
    padding-left: 14px;
}
QPushButton#modeBtn:checked {
    background-color: #7A5C30;
    color: #F7F1E8;
    border-color: #5A3E18;
    font-weight: 600;
}
QPushButton#modeBtn:hover:!checked {
    background-color: #D0C3A8;
}

/* ---- Connect (green) / Disconnect (muted red) ---- */
QPushButton#connectBtn {
    background-color: #7A9E6A;
    color: white;
    border-color: #5A7E4A;
}
QPushButton#connectBtn:hover {
    background-color: #8AAE7A;
}
QPushButton#connectBtn:disabled {
    background-color: #B8C8B0;
    color: #E8F0E4;
    border-color: #A0B898;
}
QPushButton#disconnectBtn {
    background-color: #B05040;
    color: white;
    border-color: #904030;
}
QPushButton#disconnectBtn:hover {
    background-color: #C06050;
}
QPushButton#disconnectBtn:disabled {
    background-color: #D8C0B8;
    color: #F0E8E4;
    border-color: #C8B0A8;
}

/* ---- Erase button (warm brown accent) ---- */
QPushButton#eraseBtn {
    background-color: #A07850;
    color: white;
    border-color: #785830;
}
QPushButton#eraseBtn:hover {
    background-color: #B08860;
}
QPushButton#eraseBtn:disabled {
    background-color: #D8C8B0;
    color: #F0E8DC;
    border-color: #C8B898;
}

/* ---- Emergency stop ---- */
QPushButton#stopBtn {
    background-color: #B03020;
    color: white;
    font-weight: 700;
    font-size: 10pt;
    border: 2px solid #882010;
    border-radius: 5px;
    min-height: 34px;
}
QPushButton#stopBtn:hover {
    background-color: #C04030;
}
QPushButton#stopBtn:pressed {
    background-color: #902010;
}

/* ---- COM port dropdown ---- */
QComboBox {
    font-family: 'Segoe UI', sans-serif;
    font-size: 10pt;
    background-color: #FDFAF5;
    border: 1px solid #C4B490;
    border-radius: 4px;
    padding: 4px 8px;
    min-height: 28px;
    color: #3A2A10;
}
QComboBox:disabled {
    background-color: #EDE8DD;
    color: #B8A888;
}
QComboBox::drop-down {
    border: none;
}

/* ---- Canvas group box ---- */
QFrame#canvasFrame {
    background-color: #F7F1E8;
    border: 1px solid #D4C4A8;
    border-radius: 8px;
}

/* ---- Horizontal divider ---- */
QFrame#divider {
    background-color: #D4C4A8;
    max-height: 1px;
}
"""


 
#/////////////////////////////////////////////////////////////////////////////////////////////////////
#====================Thread for USB serial communication with the Arduino ============================
#/////////////////////////////////////////////////////////////////////////////////////////////////////
 
class SerialThread(QThread):
 
    line_received      = Signal(str)   # store line recieved from serial
    connection_changed = Signal(bool)  # save if connection is established or not
 
    # initialize thread
    def __init__(self, parent=None):
        super().__init__(parent)
        self._serial  = None
        self._running = False
 
    # connect to the specified COM port
    def connect_to_port(self, port: str) -> bool:
        try:
            self._serial = serial.Serial(port, baudrate=115200, timeout=0.1)
            self._serial.dtr = True   # required for R4 Minima
            self._running = True
            self.connection_changed.emit(True)
            return True
        except serial.SerialException as e:     # print connection error
            print(f"Connection failed: {e}")
            return False
 
    def disconnect(self):
        self._running = False
        self.wait(2000)
        if self._serial and self._serial.is_open:
            self._serial.close()
        self._serial = None
        self.connection_changed.emit(False)
 
    # send an ASCII command string to the Arduino
    def send_command(self, cmd: str):
        if self._serial and self._serial.is_open:
            self._serial.write((cmd + "\n").encode("ascii"))
 
    @staticmethod
    def list_ports() -> list[str]:
        return [p.device for p in serial.tools.list_ports.comports()]
 
    def run(self):
        buf = ""
        while self._running:
            try:
                data = self._serial.read(self._serial.in_waiting or 1)
                if data:
                    buf += data.decode("ascii", errors="replace")
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip()
                        if line:
                            self.line_received.emit(line)
            except serial.SerialException:
                self.connection_changed.emit(False)
                self._running = False

#/////////////////////////////////////////////////////////////////////////////////////////////////////
# =============MainWindow class — central widget with left control panel and right canvas/status======
#/////////////////////////////////////////////////////////////////////////////////////////////////////

class MainWindow(QMainWindow):

    # Direction strings sent by the Arduino in joystick mode
    _DIRECTIONS = {
        "RIGHT", "UPRIGHT30", "UPRIGHT45", "UPRIGHT60",
        "UP",
        "UPLEFT60", "UPLEFT45", "UPLEFT30",
        "LEFT",
        "DOWNLEFT30", "DOWNLEFT45", "DOWNLEFT60",
        "DOWN",
        "DOWNRIGHT60", "DOWNRIGHT45", "DOWNRIGHT30",
        "NEUTRAL",
    }

    def __init__(self):
        super().__init__()
        self.setWindowTitle(WINDOW_TITLE)
        self.setMinimumSize(WINDOW_MIN_W, WINDOW_MIN_H)
        self.setStyleSheet(STYLE_SHEET)

        self._serial       = SerialThread(self)
        self._connected    = False
        self._current_mode = None
        self._erasing      = False

        # Root layout: left panel + right panel side by side
        root = QWidget()
        root.setObjectName("root")
        self.setCentralWidget(root)
        root_layout = QHBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)

        # Build canvas first — action buttons need a reference to it
        self._canvas = SandCanvas()
        self._canvas.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        root_layout.addWidget(self._build_left_panel(), stretch=0)
        root_layout.addWidget(self._build_right_panel(), stretch=1)

        self._connect_signals()

        # Refresh COM port list every 3 seconds
        self._port_timer = QTimer(self)
        self._port_timer.timeout.connect(self._refresh_ports)
        self._port_timer.start(3000)
        self._refresh_ports()

    #/////////////////////////////////////////////////////////////////////////////////////////////////////
    #=====================Left panel with connection, mode, and action controls ==========================
    #/////////////////////////////////////////////////////////////////////////////////////////////////////

    def _build_left_panel(self) -> QWidget:
        panel = QWidget()
        panel.setObjectName("leftPanel")
        panel.setFixedWidth(240)

        layout = QVBoxLayout(panel)
        layout.setContentsMargins(16, 20, 16, 16)
        layout.setSpacing(6)

        # --- COM Port ---
        layout.addWidget(self._make_section_label("COM Port"))
        layout.addWidget(self._build_port_row())
        layout.addWidget(self._build_connect_row())

        layout.addWidget(self._make_divider())

        # --- Mode Selection ---
        layout.addWidget(self._make_section_label("Mode"))
        layout.addWidget(self._build_mode_buttons())

        layout.addWidget(self._make_divider())

        # --- Actions ---
        layout.addWidget(self._make_section_label("Actions"))
        layout.addWidget(self._build_action_buttons())

        layout.addStretch()

        # Emergency stop pinned to bottom
        self._stop_btn = QPushButton("Emergency Stop")
        self._stop_btn.setObjectName("stopBtn")
        self._stop_btn.clicked.connect(self._on_emergency_stop)
        layout.addWidget(self._stop_btn)

        return panel

    def _build_port_row(self) -> QWidget:
        row = QWidget()
        h   = QHBoxLayout(row)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(6)

        self._port_combo = QComboBox()
        self._port_combo.setPlaceholderText("Select port...")

        self._refresh_btn = QPushButton("↻")
        self._refresh_btn.setFixedWidth(34)
        self._refresh_btn.clicked.connect(self._refresh_ports)

        h.addWidget(self._port_combo, stretch=1)
        h.addWidget(self._refresh_btn)
        return row

    def _build_connect_row(self) -> QWidget:
        row = QWidget()
        h   = QHBoxLayout(row)
        h.setContentsMargins(0, 0, 0, 0)
        h.setSpacing(6)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setObjectName("connectBtn")
        self._connect_btn.clicked.connect(self._on_connect)

        self._disconnect_btn = QPushButton("Disconnect")
        self._disconnect_btn.setObjectName("disconnectBtn")
        self._disconnect_btn.setEnabled(False)
        self._disconnect_btn.clicked.connect(self._on_disconnect)

        h.addWidget(self._connect_btn)
        h.addWidget(self._disconnect_btn)
        return row

    def _build_mode_buttons(self) -> QWidget:
        container = QWidget()
        v         = QVBoxLayout(container)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(5)

        self._mode_buttons: dict[str, QPushButton] = {}
        for label, cmd in [
            ("Clock Mode",       "SETMODE,CLOCK"),
            ("Joystick Mode",    "SETMODE,JOYSTICK"),
            ("Coordinate Mode",  "SETMODE,COORDINATE"),
        ]:
            btn = QPushButton(label)
            btn.setObjectName("modeBtn")
            btn.setCheckable(True)
            btn.setEnabled(False)
            btn.clicked.connect(lambda checked, c=cmd: self._on_mode_select(c))
            v.addWidget(btn)
            self._mode_buttons[cmd] = btn

        return container

    def _build_action_buttons(self) -> QWidget:
        container = QWidget()
        v         = QVBoxLayout(container)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(5)

        self._home_btn = QPushButton("Home")
        self._home_btn.setEnabled(False)
        self._home_btn.clicked.connect(self._on_home)
        v.addWidget(self._home_btn)

        self._erase_btn = QPushButton("Erase Sand")
        self._erase_btn.setObjectName("eraseBtn")
        self._erase_btn.setEnabled(False)
        self._erase_btn.clicked.connect(self._on_erase)
        v.addWidget(self._erase_btn)

        self._clear_btn = QPushButton("Clear Canvas")
        self._clear_btn.clicked.connect(self._canvas.clear_trail)
        v.addWidget(self._clear_btn)

        return container

    #/////////////////////////////////////////////////////////////////////////////////////////////////////
    #=====================Right panel with sand canvas preview and status bar ============================
    #/////////////////////////////////////////////////////////////////////////////////////////////////////

    def _build_right_panel(self) -> QWidget:
        panel  = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(16, 16, 16, 12)
        layout.setSpacing(10)

        # Canvas frame with border
        canvas_frame = QFrame()
        canvas_frame.setObjectName("canvasFrame")
        canvas_layout = QVBoxLayout(canvas_frame)
        canvas_layout.setContentsMargins(8, 8, 8, 8)
        canvas_layout.addWidget(self._canvas)
        layout.addWidget(canvas_frame, stretch=1)

        # Status bar — three labels side by side
        self._status_label = QLabel("Status: Disconnected")
        self._status_label.setObjectName("statusLabel")

        self._pos_label = QLabel("Position: —")
        self._pos_label.setObjectName("statusLabel")

        self._joy_label = QLabel("Direction: —")
        self._joy_label.setObjectName("statusLabel")

        status_row = QHBoxLayout()
        status_row.setSpacing(8)
        status_row.addWidget(self._status_label, stretch=2)
        status_row.addWidget(self._pos_label,    stretch=1)
        status_row.addWidget(self._joy_label,    stretch=1)
        layout.addLayout(status_row)

        return panel

    def _make_section_label(self, text: str) -> QLabel:
        lbl = QLabel(text.upper())
        lbl.setObjectName("sectionLabel")
        lbl.setContentsMargins(0, 8, 0, 2)
        return lbl

    def _make_divider(self) -> QFrame:
        line = QFrame()
        line.setObjectName("divider")
        line.setFrameShape(QFrame.HLine)
        line.setContentsMargins(0, 4, 0, 4)
        return line

    #/////////////////////////////////////////////////////////////////////////////////////////////////////
    #=====================Serial communication and message handling =======================================
    #/////////////////////////////////////////////////////////////////////////////////////////////////////

    def _connect_signals(self):
        self._serial.line_received.connect(self._on_line_received)
        self._serial.connection_changed.connect(self._on_connection_changed)

    def _on_line_received(self, line: str):
        upper = line.upper()

        if upper == "READY":
            self._status_label.setText("Status: Ready")
            for btn in self._mode_buttons.values():
                btn.setEnabled(True)
            self._home_btn.setEnabled(True)
            self._erase_btn.setEnabled(True)

        elif upper.startswith("POS,"):
            parts = line.split(",")
            if len(parts) == 3:
                try:
                    x, y = float(parts[1]), float(parts[2])
                    self._canvas.update_position(x, y)
                    self._pos_label.setText(f"Position: {x:.1f}, {y:.1f}")
                except ValueError:
                    pass

        elif upper.startswith("OK,MODE,"):
            mode = line.split(",")[2].upper()
            self._set_active_mode_btn("SETMODE," + mode)
            self._canvas.clear_trail()
            self._status_label.setText(f"Status: {mode.title()} mode")

        elif upper == "OK,HOME":
            self._status_label.setText("Status: Idle")
            self._canvas.clear_trail()
            self._set_actions_enabled(True)

        elif upper == "OK,STOP":
            self._status_label.setText("Status: Stopped")
            self._erasing = False
            self._erase_btn.setEnabled(True)

        elif upper == "OK,ERASE":
            self._erasing = False
            self._erase_btn.setEnabled(True)
            self._status_label.setText("Status: Erase complete")

        elif upper.startswith("STATUS,"):
            parts = line.split(",")
            if len(parts) == 5:
                self._status_label.setText(f"Status: {parts[4]}  pos known: {parts[1]}")
                self._pos_label.setText(f"Position: {parts[2]}, {parts[3]}")
                for btn in self._mode_buttons.values():
                    btn.setEnabled(True)
                self._home_btn.setEnabled(True)
                self._erase_btn.setEnabled(True)

        elif upper.startswith("INFO,"):
            msg = line[5:]
            self._status_label.setText(f"Status: {msg.replace('_', ' ').title()}")
            if msg.upper() == "ERASING":
                self._erasing = True
                self._erase_btn.setEnabled(False)
                self._canvas.clear_trail()
            elif msg.upper() == "HOMING":
                self._set_actions_enabled(False)

        elif upper.startswith("LIMIT,"):
            self._status_label.setText(f"Limit hit: {line[6:]} — home required")
            if self._erasing:
                self._erasing = False
                self._erase_btn.setEnabled(True)

        elif upper.startswith("ERR,"):
            self._status_label.setText(f"Error: {line[4:]}")
            if self._erasing:
                self._erasing = False
                self._erase_btn.setEnabled(True)

        elif upper in self._DIRECTIONS:
            self._joy_label.setText(f"Direction: {line}")

    def _on_connection_changed(self, connected: bool):
        self._connected = connected
        self._connect_btn.setEnabled(not connected)
        self._disconnect_btn.setEnabled(connected)

        # Buttons disabled until READY received after connection
        self._home_btn.setEnabled(False)
        self._erase_btn.setEnabled(False)
        for btn in self._mode_buttons.values():
            btn.setEnabled(False)

        if connected:
            self._status_label.setText("Status: Waiting for ready...")
        else:
            self._status_label.setText("Status: Disconnected")
            self._pos_label.setText("Position: ")
            self._joy_label.setText("Direction: ")
            self._current_mode = None
            self._erasing      = False
            for btn in self._mode_buttons.values():
                btn.setChecked(False)

    def _refresh_ports(self):
        current = self._port_combo.currentText()
        self._port_combo.clear()
        self._port_combo.addItems(SerialThread.list_ports())
        idx = self._port_combo.findText(current)
        if idx >= 0:
            self._port_combo.setCurrentIndex(idx)

    def _on_connect(self):
        port = self._port_combo.currentText()
        if not port:
            return
        if self._serial.connect_to_port(port):
            self._serial.start()                        # Arduino resets on connect, READY may be sent before reading
            QTimer.singleShot(3000, lambda: self._serial.send_command("STATUS"))    # Send STATUS after 3s as a fallback to enable buttons if READY was missed

    def _on_disconnect(self):
        self._serial.disconnect()

    def _on_mode_select(self, cmd: str):
        self._send(cmd)
        self._current_mode = cmd

    def _on_home(self):
        self._send("HOME")
        self._set_actions_enabled(False)

    def _on_erase(self):
        self._send("ERASE")
        self._erase_btn.setEnabled(False)
        self._canvas.clear_trail()

    def _on_emergency_stop(self):
        self._send("STOP")
        self._status_label.setText("Status: Stopped")
        self._erasing = False
        self._erase_btn.setEnabled(self._connected)

    #/////////////////////////////////////////////////////////////////////////////////////////////////////
    #================================= Drawing and coordinate conversions ================================
    #/////////////////////////////////////////////////////////////////////////////////////////////////////

    def _send(self, cmd: str):
        self._serial.send_command(cmd)

    def _set_active_mode_btn(self, active_cmd: str):
        for cmd, btn in self._mode_buttons.items():
            btn.setChecked(cmd == active_cmd)
        self._current_mode = active_cmd

    def _set_actions_enabled(self, enabled: bool):
        self._home_btn.setEnabled(enabled and self._connected)
        self._erase_btn.setEnabled(enabled and self._connected)
        for btn in self._mode_buttons.values():
            btn.setEnabled(enabled and self._connected)

    def closeEvent(self, event):
        if self._connected:
            self._serial.disconnect()
        event.accept()


#/////////////////////////////////////////////////////////////////////////////////////////////////////
#======Main application entry point — create QApplication, show MainWindow, and start event loop======
#/////////////////////////////////////////////////////////////////////////////////////////////////////

def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()