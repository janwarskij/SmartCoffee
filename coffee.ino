#include <LiquidCrystalRus.h>
#include <EEPROM.h>

// LCD: RS, E, D4, D5, D6, D7
LiquidCrystalRus lcd(12, 13, 4, 5, 6, 8);

// Пины
#define HEATER_PIN     7
#define BUZZER_PIN     9
#define PUMP_PIN       3
#define STRENGTH_POT   A1
#define START_BUTTON_INT 2  // Пин D2 для прерывания INT0

// Глобальные флаги для прерываний
volatile bool buttonPressed = false;
volatile unsigned long lastInterruptTime = 0;
const unsigned long debounceDelay = 200;  // Защита от дребезга (мс)

// Обработчик прерывания с защитой от дребезга
void handleButtonInterrupt() {
  unsigned long interruptTime = millis();
  if (interruptTime - lastInterruptTime > debounceDelay) {
    buttonPressed = true;
  }
  lastInterruptTime = interruptTime;
}

class ResourceManager {
  public:
    byte beanAmount;  // Используем 1 байт (макс. 255 зёрен)
    byte waterCups;   // Используем 1 байт для воды (0-3 кружки)

    ResourceManager() {
      // Чтение значений из EEPROM при старте
      beanAmount = EEPROM.read(0);  // 1 байт для зёрен
      waterCups = EEPROM.read(1);   // 1 байт для воды
    }

    void refillWater() {
      waterCups = 3;
      EEPROM.write(1, waterCups);  // Сохранение значения воды в EEPROM
      Serial.println("Вода восстановлена");
    }

    void refillBeans() {
      beanAmount = 100;
      EEPROM.write(0, beanAmount);  // Сохранение в 1 байте
      Serial.println("Зерна восстановлены");
    }

    void consumeBeans(int strengthCost) {
      beanAmount -= strengthCost;
      if (beanAmount < 0) beanAmount = 0;
      EEPROM.write(0, beanAmount);  // Сохранение в 1 байте
    }
};

class StrengthSelector {
  public:
    String strength = "medium";  // "weak", "medium", "strong"
    int strengthCost = 20;

    void readStrength() {
      int potValue = analogRead(STRENGTH_POT);
      if (potValue < 341) {
        strength = "weak";
        strengthCost = 10;
      } else if (potValue < 682) {
        strength = "medium";
        strengthCost = 20;
      } else {
        strength = "strong";
        strengthCost = 30;
      }
    }
};

class CoffeeMachine {
  private:
    bool brewing = false;
    bool alarmed = false;
    bool problem = false;
    unsigned long brewStartTime = 0;
    const unsigned long brewDuration = 7000;  // Продолжительность варки (7 сек)
    ResourceManager resourceManager;
    StrengthSelector strengthSelector;

  public:
    void setup() {
      // Настройка прерывания для кнопки START
      pinMode(START_BUTTON_INT, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(START_BUTTON_INT), handleButtonInterrupt, FALLING);
      
      // Настройка остальных пинов
      pinMode(BUZZER_PIN, OUTPUT);
      pinMode(HEATER_PIN, OUTPUT);
      pinMode(PUMP_PIN, OUTPUT);

      // Инициализация LCD
      lcd.begin(16, 2);
      lcd.print("Smart Coffee");
      delay(2000);
      lcd.clear();

      Serial.begin(9600);
    }

    void loop() {
      strengthSelector.readStrength();
      displayStatus();
      
      // Проверка ресурсов
      checkResources();

      // Обработка прерывания кнопки
      if (buttonPressed) {
        buttonPressed = false;  // Сбрасываем флаг
        handleButtonAction();
      }

      handleSerial();
      delay(100);  // Небольшая задержка для стабильности
    }

    void checkResources() {
      if ((resourceManager.waterCups == 0) || (resourceManager.beanAmount == 0)) {
        problem = true;
        if (!alarmed) {
          showResourceError();
        }
      } else {
        problem = false;
        alarmed = false;
      }
    }

    void showResourceError() {
      lcd.clear();
      if (resourceManager.waterCups == 0) {
        lcd.print("Ошибка: нет воды");
      } else {
        lcd.print("Ошибка:");
        lcd.setCursor(0, 1);
        lcd.print("нет зёрен");
      }
      playTripleBeep();
    }

    void handleButtonAction() {
      if (problem && !alarmed) {
        acknowledgeProblem();
      } else if (problem && alarmed) {
        showResourceError();
      } else if (!brewing) {
        attemptStartBrewing();
      } 
    }

    void acknowledgeProblem() {
      lcd.clear();
      lcd.print("Вы предупреждены");
      alarmed = true;
      delay(5000);
      lcd.clear();
    }

    void attemptStartBrewing() {
      if (resourceManager.beanAmount < strengthSelector.strengthCost) {
        showInsufficientBeans();
      } else {
        startBrewing();
      }
    }

    void showInsufficientBeans() {
      lcd.clear();
      lcd.print("Недостаточно");
      lcd.setCursor(0, 1);
      lcd.print("зёрен для режима");
      playTripleBeep();
    }

    void startBrewing() {
      // Подготовка ресурсов
      resourceManager.waterCups--;
      resourceManager.consumeBeans(strengthSelector.strengthCost);
      EEPROM.update(1, resourceManager.waterCups);
      EEPROM.update(0, resourceManager.beanAmount);

      // Запуск процесса
      brewing = true;
      digitalWrite(HEATER_PIN, HIGH);
      digitalWrite(PUMP_PIN, HIGH);
      showBrewingProgress();
      stopBrewing();
    }

    void showBrewingProgress() {
      lcd.clear();
      lcd.print("Варим кофе...");
      Serial.println("Варим кофе");

      
      unsigned long startTime = millis();
      while (millis() - startTime < brewDuration) {
        int progress = map(millis() - startTime, 0, brewDuration, 0, 16);
        
        lcd.setCursor(0, 1);
        for (int i = 0; i < progress; i++) {
          lcd.print("-");
        }
        delay(100);
      }
    }

    void stopBrewing() {
      brewing = false;
      digitalWrite(HEATER_PIN, LOW);
      digitalWrite(PUMP_PIN, LOW);
      
      lcd.clear();
      lcd.print("Готово!");
      Serial.print("Готово!");
      playCompletionBeep();
      
      Serial.print("Зерна: ");
      Serial.print(resourceManager.beanAmount);
      Serial.print(", Вода: ");
      Serial.println(resourceManager.waterCups);
    }

    void playTripleBeep() {
      for (int i = 0; i < 3; i++) {
        tone(BUZZER_PIN, 3000, 1000);
        delay(2000);
      }
    }

    void playCompletionBeep() {
      tone(BUZZER_PIN, 2000, 500);
      delay(1000);
      tone(BUZZER_PIN, 2000, 500);
      delay(1000);
    }

    void displayStatus() {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Режим: ");
      lcd.print(strengthSelector.strength == "weak" ? "Слабый " : 
               strengthSelector.strength == "medium" ? "Средний" : "Крепкий ");

      lcd.setCursor(0, 1);
      lcd.print("Зерна:");
      lcd.print(resourceManager.beanAmount);
      lcd.print(" Вода:");
      lcd.print(resourceManager.waterCups);
    }

    void handleSerial() {
      if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input == "refill_water") {
          resourceManager.refillWater();
        }
        else if (input == "refill_beans") {
          resourceManager.refillBeans();
        }
        else if (input == "status") {
          Serial.print("Зерна: ");
          Serial.print(resourceManager.beanAmount);
          Serial.print(", Вода: ");
          Serial.println(resourceManager.waterCups);
        }
        else if (input.startsWith("brew")) {
          handleBrewCommand(input);
        }
      }
    }

    void handleBrewCommand(String input) {
    // Установка типа кофе и расхода зёрен
    if (input == "brew weak") {
        strengthSelector.strength = "weak";
        strengthSelector.strengthCost = 10;
    } 
    else if (input == "brew medium") {
        strengthSelector.strength = "medium";
        strengthSelector.strengthCost = 20;
    } 
    else if (input == "brew strong") {
        strengthSelector.strength = "strong";
        strengthSelector.strengthCost = 30;
    } 
    else if (input != "brew") {  // Некорректная команда
        Serial.println("Неизвестная команда варки");
        return;
    }

    // Проверка ресурсов перед запуском
    if (resourceManager.waterCups == 0) {
        Serial.println("Ошибка: нет воды");
        playTripleBeep();
        return;
    }
    
    if (resourceManager.beanAmount == 0) {
        Serial.println("Нет зёрен вообще");
        playTripleBeep();
        return;
    }
    
    if (resourceManager.beanAmount < strengthSelector.strengthCost) {
        Serial.println("Недостаточно зёрен для режима");
        playTripleBeep();
        return;
    }

    // Если все проверки пройдены - запускаем варку
    startBrewing();
    }
};

CoffeeMachine coffeeMachine;

void setup() {
  coffeeMachine.setup();
}

void loop() {
  coffeeMachine.loop();
}