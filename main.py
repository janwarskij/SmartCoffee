import threading
import time
from datetime import datetime

import serial
import serial.tools.list_ports
from flask import Flask, render_template, request, jsonify

app = Flask(__name__)

# Настройки подключения
BAUD_RATE = 9600
CONNECTION_TIMEOUT = 3  # сек

# Стоимость варки для разных типов кофе
COFFEE_COSTS = {
    'weak': 10,
    'medium': 20,
    'strong': 30
}

# Глобальное состояние системы
system_state = {
    'status': "Инициализация...",
    'water': None,
    'beans': None,
    'last_update': None,
    'logs': [],
    'arduino_connected': False,
    'initialized': False,
    'is_brewing': False  # Флаг процесса варки
}

arduino = None


def log_message(message):
    """Логирование сообщений с временной меткой"""
    timestamp = datetime.now().strftime("%H:%M:%S")
    full_message = f"[{timestamp}] {message}"
    system_state['logs'].append(full_message)
    if len(system_state['logs']) > 20:
        system_state['logs'].pop(0)


def connect_arduino():
    """Подключение к Arduino"""
    global arduino
    try:
        if arduino and arduino.is_open:
            arduino.close()

        ports = serial.tools.list_ports.comports()
        for port in ports:
            if 'Arduino' in port.description or 'CH340' in port.description:
                arduino = serial.Serial(port.device, BAUD_RATE, timeout=1)
                time.sleep(CONNECTION_TIMEOUT)
                system_state['arduino_connected'] = True
                log_message(f"Успешное подключение к {port.device}")
                return True

        log_message("Arduino не найден!")
        system_state['arduino_connected'] = False
        return False

    except Exception as e:
        log_message(f"Ошибка подключения: {str(e)}")
        system_state['arduino_connected'] = False
        return False


def send_command(command):
    """Отправка команды на Arduino"""
    if not arduino or not arduino.is_open:
        if not connect_arduino():
            return False

    try:
        arduino.write(f"{command}\n".encode())
        log_message(f"Отправлена команда: {command}")
        return True
    except Exception as e:
        log_message(f"Ошибка отправки команды: {str(e)}")
        system_state['arduino_connected'] = False
        return False


def process_serial_data(data):
    """Обработка входящих данных от Arduino"""
    try:
        if "Зерна:" in data and "Вода:" in data:
            parts = data.replace(',', '').split()
            system_state['beans'] = int(parts[1])
            system_state['water'] = int(parts[3])
            system_state['last_update'] = datetime.now()

            if not system_state['initialized']:
                system_state['initialized'] = True
                system_state['status'] = "Готов к работе"

            log_message(f"Обновление: вода={system_state['water']}, зёрна={system_state['beans']}")

        # Обработка статусов
        elif "Варим кофе" in data:
            system_state['is_brewing'] = True
            system_state['status'] = "Варим кофе..."
            log_message("Начало процесса варки")
        elif "Готово!" in data:
            system_state['is_brewing'] = False
            system_state['status'] = "Кофе готов!"
            log_message("Процесс варки завершен")
        elif "Вода восстановлена" in data:
            system_state['water'] = 3
            system_state['status'] = "Вода долита"
            log_message("Резервуар с водой пополнен")
        elif "Зерна восстановлены" in data:
            system_state['beans'] = 100
            system_state['status'] = "Зёрна дополнены"
            log_message("Резервуар с зёрнами пополнен")
        elif "Недостаточно зёрен" in data:
            system_state['status'] = "Ошибка: недостаточно зёрен"
            log_message("Попытка варки при недостатке зёрен")

    except Exception as e:
        log_message(f"Ошибка обработки данных: {str(e)}")


def serial_read_thread():
    """Поток для чтения данных из последовательного порта"""
    while True:
        if arduino and arduino.is_open:
            try:
                if arduino.in_waiting:
                    line = arduino.readline().decode().strip()
                    if line:
                        process_serial_data(line)
            except Exception as e:
                log_message(f"Ошибка чтения: {str(e)}")
                system_state['arduino_connected'] = False
                time.sleep(1)
        else:
            time.sleep(1)


# Инициализация при старте
if connect_arduino():
    threading.Thread(target=serial_read_thread, daemon=True).start()


@app.route('/')
def index():
    if arduino and arduino.is_open:
        send_command("status")
    return render_template('index.html')


@app.route('/status')
def get_status():
    if not system_state['initialized']:
        return jsonify({
            'status': "Ожидание данных от Arduino...",
            'water': None,
            'beans': None,
            'logs': system_state['logs'],
            'initialized': False,
            'connected': system_state['arduino_connected'],
            'is_brewing': False
        })

    if system_state['water'] == 0:
        system_state['status'] = "Пополните воду!"
    elif system_state['beans'] == 0:
        system_state['status'] = "Пополните зёрна!"

    return jsonify({
        'status': system_state['status'],
        'water': system_state['water'],
        'beans': system_state['beans'],
        'logs': system_state['logs'],
        'last_update': system_state['last_update'].strftime("%H:%M:%S") if system_state['last_update'] else None,
        'initialized': system_state['initialized'],
        'connected': system_state['arduino_connected'],
        'is_brewing': system_state['is_brewing']
    })


@app.route('/command', methods=['POST'])
def handle_command():
    command = request.form.get('command')
    if command:
        # Проверка достаточности зёрен перед варкой
        if command.startswith('brew'):
            coffee_type = command.split()[1]
            if system_state['beans'] is not None and system_state['beans'] < COFFEE_COSTS[coffee_type]:
                log_message(f"Недостаточно зёрен для {coffee_type} (нужно {COFFEE_COSTS[coffee_type]})")
                return jsonify({
                    'success': False,
                    'message': f"Недостаточно зёрен для {coffee_type} кофе (нужно {COFFEE_COSTS[coffee_type]})"
                })

        success = send_command(command)
        if success and not command.startswith('status'):
            time.sleep(1)
            send_command('status')
        return jsonify({'success': success})
    return jsonify({'success': False})


if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', use_reloader=False)