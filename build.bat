@echo off
rem One-click build script (close running EasySerial.exe before building)
python -m PyInstaller --noconfirm --onefile --windowed --name EasySerial --hidden-import serial.tools.list_ports --hidden-import serial.tools.list_ports_windows --hidden-import PyQt6.QtCore --hidden-import PyQt6.QtGui --hidden-import PyQt6.QtWidgets main.py
echo.
echo Done: dist\EasySerial.exe
pause
