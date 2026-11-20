@echo off
rem Start the TrashNet server + web UI at http://localhost:8000
cd /d "%~dp0server"
if not exist ".venv" python -m venv .venv
call .venv\Scripts\activate.bat
pip install -q -r requirements.txt
python main.py --host 0.0.0.0 --port 8000
