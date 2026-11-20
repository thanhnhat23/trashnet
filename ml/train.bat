@echo off
rem Full pipeline: dataset -> degrade -> train -> calibrate/export. Run from the project root.
setlocal
pip install -r ml\requirements.txt || exit /b 1
python ml\build_dataset.py || exit /b 1
python ml\degrade.py || exit /b 1
python ml\train.py --device 0 || exit /b 1
python ml\export.py || exit /b 1
echo Done. Models in server\models and mobile\assets\models
