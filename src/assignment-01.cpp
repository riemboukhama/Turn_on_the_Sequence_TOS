#include <LiquidCrystal_I2C.h>
#include <avr/sleep.h>

#define POT_PIN A0

#define LS 3
#define L1 11
#define L2 10
#define L3 9
#define L4 8

#define B1 2
#define B2 7
#define B3 6
#define B4 5

const int ledArray[4] = {L1, L2, L3, L4};
const int buttonArray[4] = {B1, B2, B3, B4};

// Array per la sequenza di ogni round
byte digits[4];

// Variabili per fading led rosso
int brightness = 0;
int fadeAmount = 5;
long lastFadeTime = 0; // Per il fading non bloccante

// Variabili di stato
long T1 = 8000; // Tempo base
long F = 500;   // Fattore di difficoltà iniziale '1' di mezzo secondo
int score = 0;

// Definizione degli stati
enum GameState {
  MENU,
  STARTING_GAME,
  PREPARE_ROUND,
  WAITING_INPUT,
  WAITING_RELEASE,
  ROUND_PASSED,
  GAME_OVER_LED_ON,
  GAME_OVER_LED_OFF
};

GameState currentState = MENU;

// Variabili per gestire il tempo
long stateStartTime = 0;
long roundStartTime = 0;
long menuIdleStartTime = 0;
long debounceTime = 0;

int currentSequenceIndex = 0;
int lastPressedButtonIndex = -1;
bool menuLcdNeedsUpdate = true;

LiquidCrystal_I2C lcd(0x27, 16, 4);

void setup() {
  lcd.init();            
  lcd.backlight();
  
  // Pin led
  pinMode(LS, OUTPUT);
  for(int i = 0; i < 4; i++) {
    pinMode(ledArray[i], OUTPUT);
  }
  
  // Pin bottoni
  for(int i = 0; i < 4; i++) {
    pinMode(buttonArray[i], INPUT_PULLUP);
  }       
  
  // Seme del random
  randomSeed(analogRead(A1));
  Serial.begin(9600);
  
  menuIdleStartTime = millis();
}

void loop() {
  switch(currentState) {
    
    case MENU:
      handleMenu();
      break;

    case STARTING_GAME:
      if (millis() - stateStartTime >= 2000) {
        score = 0;   // Azzera il punteggio
        T1 = 8000;
        currentState = PREPARE_ROUND;
      }
      break;

    case PREPARE_ROUND:
      // Inizio dei round
      handlePrepareRound();
      break;

    case WAITING_INPUT:
      // Aspetta che premo o scada il tempo
      handleWaitingInput();
      break;

    case WAITING_RELEASE:
      handleWaitingRelease();
      break;

    case ROUND_PASSED:
      // Attesa di 3 secondi prima del prossimo round
      if (millis() - stateStartTime >= 3000) {
        for(int i = 0; i < 4; i++) {
          digitalWrite(ledArray[i], LOW);
        }
        currentState = PREPARE_ROUND;
      }
      break;

    case GAME_OVER_LED_ON:
      // Attende 2 secondi col LED acceso
      if (millis() - stateStartTime >= 2000) {
        digitalWrite(LS, LOW);  // Spegni LED
        stateStartTime = millis();
        currentState = GAME_OVER_LED_OFF;
      }
      break;

    case GAME_OVER_LED_OFF:
      // Attende 8 secondi prima di tornare al menu
      if (millis() - stateStartTime >= 8000) {
        menuLcdNeedsUpdate = true;
        menuIdleStartTime = millis();
        
        // Torna al menu
        T1 = 8000;
        score = 0; // Azzera il punteggio
        currentState = MENU;
      }
      break;
  }
}

void handleMenu() {
  
  long old_F = F;
  F = readDifficulty();
  bool difficultyChanged = (F != old_F);

  // Se il potenziometro viene usato, resetta il timer dello sleep
  if (difficultyChanged) {
    menuIdleStartTime = millis(); 
  }

  // Aggiorna lo schermo se si arriva dal Game Over/Avvio, o se la difficoltà cambia
  if (menuLcdNeedsUpdate || difficultyChanged) {
    
    if (menuLcdNeedsUpdate) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Welcome to TOS!");
      lcd.setCursor(0, 1);
      lcd.print("Press B1 to Start");
      menuLcdNeedsUpdate = false;
    }
    
    lcd.setCursor(0, 3);
    switch (F){
      case 500:
        lcd.print("Difficulty: 1");
        break;
      case 650:
        lcd.print("Difficulty: 2");
        break;
      case 800:
        lcd.print("Difficulty: 3");
        break;
      default:
        lcd.print("Difficulty: 4");
        break;
    }
  }

  // Verifica per entrare in sleep mode
  if(millis() - menuIdleStartTime >= 10000){
    attachInterrupt(digitalPinToInterrupt(B1), wakeUp, LOW);
    analogWrite(LS, 0);
    lcd.clear();
    lcd.print("Entering SleepMode..");
    delay(1500);
    lcd.noBacklight();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sleep_mode();
    sleep_disable();
    detachInterrupt(digitalPinToInterrupt(B1));

    while(digitalRead(B1) == LOW); 
    delay(100); // Debounce

    lcd.backlight();
    menuLcdNeedsUpdate = true;
    menuIdleStartTime = millis();
    return;
  }

  // Fading
  if (millis() - lastFadeTime >= 20) {
    analogWrite(LS, brightness);
    brightness += fadeAmount;

    if (brightness <= 0 || brightness >= 255) {
      fadeAmount = -fadeAmount;
    }
    lastFadeTime = millis();
  }
  
  // Il gioco inizia se viene premuto B1
  if(digitalRead(B1) == LOW) {
    Serial.println("Premuto B1");
    analogWrite(LS, 0); // Spegne il LED
    lcd.clear();
    lcd.print("Go!");
    
    stateStartTime = millis();
    currentState = STARTING_GAME;
  }
}

void handlePrepareRound() {
  Serial.println("Entrato in gioco");
  
  // Spegne i LED verdi
  for(int i = 0; i < 4; i++) {
    digitalWrite(ledArray[i], LOW);
  }
  
  // Genera e mostra i numeri
  setRandomDigits();
  lcd.clear();
  printRandomDigits();

  currentSequenceIndex = 0;
  roundStartTime = millis();
  
  currentState = WAITING_INPUT;
}

void handleWaitingInput() {
  lcd.setCursor(0, 10);
  lcd.print("Time:");
  lcd.print((T1 + 1000 - (millis() - roundStartTime)) / 1000);

  if(millis() - roundStartTime >= T1) {
    triggerGameOver();
    return;
  }

  // Loop per indovinare i 4 bottoni
  for(int j = 0; j < 4; j++) {
    if(digitalRead(buttonArray[j]) == LOW) {
      int correctButtonIndex = digits[currentSequenceIndex] - 1;
      
      if(j == correctButtonIndex) {
        digitalWrite(ledArray[j], HIGH);
        lastPressedButtonIndex = j;
        debounceTime = millis();
        currentState = WAITING_RELEASE;
        return; 
      } 
      else {
        triggerGameOver();
        return;
      }
    }
  }
}

void handleWaitingRelease() {
  lcd.setCursor(0, 10);
  lcd.print("Time:");
  lcd.print((T1 + 1000 - (millis() - roundStartTime)) / 1000);

  if(millis() - roundStartTime >= T1) {
    triggerGameOver();
    return;
  }

  // Debounce e attesa che il bottone venga rilasciato
  if (millis() - debounceTime >= 50) {
    if (digitalRead(buttonArray[lastPressedButtonIndex]) == HIGH) {
      
      currentSequenceIndex++;
      
      if (currentSequenceIndex >= 4) {
        score++;
        
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.print("GOOD! Score: ");
        lcd.print(score);
        
        T1 -= F; // Aumenta la difficoltà riducendo il tempo
        if(T1 < 500) T1 = 500; // Limite di mezzo secondo per limitare la difficoltà
        
        stateStartTime = millis();
        currentState = ROUND_PASSED;
      } else {
        currentState = WAITING_INPUT;
      }
    }
  }
}

void triggerGameOver() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Game Over");
  lcd.setCursor(0,2);
  lcd.print("Final Score: ");
  lcd.print(score);

  for(int i = 0; i < 4; i++) {
    digitalWrite(ledArray[i], LOW);
  }
  
  digitalWrite(LS, HIGH); // LED Rosso Fisso
  stateStartTime = millis();
  currentState = GAME_OVER_LED_ON;
}

void setRandomDigits() {
  int base[4] = {1, 2, 3, 4};
  
  for (int i = 0; i < 4; i++) {
    int r = random(i, 4);
    int temp = base[i];
    base[i] = base[r];
    base[r] = temp;
  }
  
  // Salva nell'array globale per usarlo nei controlli
  digits[0] = base[0];
  digits[1] = base[1];
  digits[2] = base[2];
  digits[3] = base[3];
}

void printRandomDigits() {
  lcd.setCursor(0,0);
  for(int i = 0; i < 4; i++) {
    lcd.print(digits[i]);
  }
}

int readDifficulty(){
  int newValue = analogRead(POT_PIN);

  if(newValue <= 250){
    return 500;
  }
  else if(newValue > 250 && newValue <= 500){
    return 650;
  }
  else if(newValue > 500 && newValue <= 750){
    return 800;
  }
  else{
    return 900;
  }
}

void wakeUp(){}