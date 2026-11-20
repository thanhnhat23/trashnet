# TrashNet actuator firmware (ESP32, MG996R tray + MG995 dump + lid + 2x HC-SR04)

    idf.py set-target esp32 && idf.py build flash monitor
    sh test/run_host_tests.sh        # host unit tests for motion_math (gcc only)

Power the servos from a separate 5-6 V supply (common GND). Calibrate neutral/t90 from the Devices page, then `POST /home/set`.
Not compiled in the authoring environment: build once and report errors. API: `shared/device-api.md`.
