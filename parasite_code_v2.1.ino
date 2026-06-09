#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "BluetoothSerial.h" 

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run make menuconfig to enable it.
#endif

BluetoothSerial SerialBT; 

// ==========================================
// 1. PIN CONFIGURATION
// ==========================================
#define TRIG_PIN 4
#define ECHO_PIN 5
#define IR_PIN   36 

#define LF_HIP_PIN  13
#define LF_KNEE_PIN 12
#define RF_HIP_PIN  14
#define RF_KNEE_PIN 27
#define LR_HIP_PIN  26 
#define LR_KNEE_PIN 25
#define RR_HIP_PIN  33
#define RR_KNEE_PIN 32
#define TAIL_PIN    2  

const int servoPins[] = {LF_HIP_PIN, LF_KNEE_PIN, RF_HIP_PIN, RF_KNEE_PIN, LR_HIP_PIN, LR_KNEE_PIN, RR_HIP_PIN, RR_KNEE_PIN, TAIL_PIN};
const int totalServos = 9;

// ==========================================
// 2. OPERATIONAL ARCHITECTURE
// ==========================================
enum OperationMode { CONTROL_MODE, AUTO_MODE };
OperationMode currentOpMode = CONTROL_MODE; 

enum RobotState { IDLE, WALKING_FORWARD, WALKING_BACKWARD, TURNING_LEFT, TURNING_RIGHT, RESETTING };
RobotState currentState = IDLE; 

// ==========================================
// 3. HIGH-SPEED TIMING CONSTANTS
// ==========================================
const int phaseDuration = 60; 
unsigned long lastGaitUpdate = 0, lastFrameUpdate = 0, stateTimer = 0;
uint32_t animationFrame = 0;
int gaitPhase = 0;

// ==========================================
// 4. SERVO HOMES & AMPLITUDES 
// ==========================================
const int LF_Hip_Home  = 90;  const int LF_Knee_Home = 90;
const int RF_Hip_Home  = 90;  const int RF_Knee_Home = 90;
const int LR_Hip_Home  = 90;  const int LR_Knee_Home = 90;
const int RR_Hip_Home  = 90;  const int RR_Knee_Home = 90;
const int Tail_Home    = 90; 

const int SWING_HIP  = 20; 
const int LIFT_KNEE  = 24; 
const int TAIL_WAVE  = 25; 

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ==========================================
// 5. NATIVE ESP32 HARDWARE DRIVER
// ==========================================
void writeESP32Servo(int pin, int angle) {
  long duty = map(angle, 0, 180, 102, 512);
  analogWrite(pin, duty);
}

// ==========================================
// 6. TRANSITION ROUTINE
// ==========================================
void executeLiftAndReset() {
  Serial.println("[!] Resetting Chassis...");
  
  writeESP32Servo(LF_KNEE_PIN, LF_Knee_Home - LIFT_KNEE); 
  writeESP32Servo(RR_KNEE_PIN, RR_Knee_Home + LIFT_KNEE); 
  delay(80);
  
  writeESP32Servo(LF_HIP_PIN, LF_Hip_Home); 
  writeESP32Servo(RR_HIP_PIN, RR_Hip_Home); 
  writeESP32Servo(TAIL_PIN, Tail_Home);
  delay(80);
  
  writeESP32Servo(LF_KNEE_PIN, LF_Knee_Home); 
  writeESP32Servo(RR_KNEE_PIN, RR_Knee_Home);
  delay(80);

  writeESP32Servo(RF_KNEE_PIN, RF_Knee_Home + LIFT_KNEE); 
  writeESP32Servo(LR_KNEE_PIN, LR_Knee_Home - LIFT_KNEE); 
  delay(80);
  
  writeESP32Servo(RF_HIP_PIN, RF_Hip_Home); 
  writeESP32Servo(LR_HIP_PIN, LR_Hip_Home); 
  delay(80);
  
  writeESP32Servo(RF_KNEE_PIN, RF_Knee_Home); 
  writeESP32Servo(LR_KNEE_PIN, LR_Knee_Home);
  delay(80);

  currentState = IDLE;
}

// ==========================================
// 7. OMNIDIRECTIONAL STEERING TROT ENGINE
// ==========================================
void executeOmniTrotGait(RobotState moveMode) {
  int leftSwing  = SWING_HIP;
  int rightSwing = SWING_HIP;

  if (moveMode == WALKING_BACKWARD) {
    leftSwing  = -SWING_HIP;
    rightSwing = -SWING_HIP;
  } 
  else if (moveMode == TURNING_LEFT) {
    leftSwing  = -SWING_HIP; 
    rightSwing = SWING_HIP;
  } 
  else if (moveMode == TURNING_RIGHT) {
    leftSwing  = SWING_HIP;
    rightSwing = -SWING_HIP; 
  }

  switch (gaitPhase) {
    case 0:
      writeESP32Servo(LF_KNEE_PIN, LF_Knee_Home - LIFT_KNEE); 
      writeESP32Servo(RR_KNEE_PIN, RR_Knee_Home + LIFT_KNEE);
      writeESP32Servo(TAIL_PIN, Tail_Home - TAIL_WAVE);
      break;

    case 1:
      writeESP32Servo(LF_HIP_PIN,  LF_Hip_Home + leftSwing);  
      writeESP32Servo(LR_HIP_PIN,  LR_Hip_Home - leftSwing);  
      
      writeESP32Servo(RF_HIP_PIN,  RF_Hip_Home + rightSwing);  
      writeESP32Servo(RR_HIP_PIN,  RR_Hip_Home - rightSwing);  
      break;

    case 2:
      writeESP32Servo(LF_KNEE_PIN, LF_Knee_Home);
      writeESP32Servo(RR_KNEE_PIN, RR_Knee_Home);
      break;

    case 3:
      writeESP32Servo(RF_KNEE_PIN, RF_Knee_Home + LIFT_KNEE); 
      writeESP32Servo(LR_KNEE_PIN, LR_Knee_Home - LIFT_KNEE);
      writeESP32Servo(TAIL_PIN, Tail_Home + TAIL_WAVE);
      break;

    case 4:
      writeESP32Servo(LF_HIP_PIN,  LF_Hip_Home - leftSwing);  
      writeESP32Servo(LR_HIP_PIN,  LR_Hip_Home + leftSwing);  
      
      writeESP32Servo(RF_HIP_PIN,  RF_Hip_Home - rightSwing);  
      writeESP32Servo(RR_HIP_PIN,  RR_Hip_Home + rightSwing);  
      break;

    case 5:
      writeESP32Servo(RF_KNEE_PIN, RF_Knee_Home);
      writeESP32Servo(LR_KNEE_PIN, LR_Knee_Home);
      break;
  }
  gaitPhase = (gaitPhase + 1) % 6; 
}

// ==========================================
// 8. UPGRADED CINEMATIC ENGINE (EYEBROWS + FULLSCREEN MATCHING BACKGROUNDS)
// ==========================================
void playVideoAnimation(String action, uint32_t frame) {
  display.clearDisplay();

  // -----------------------------------------------------------
  // LAYER 1: FULLSCREEN MATCHING BACKGROUNDS
  // -----------------------------------------------------------
  if (action == "FORWARD_VIDEO") {
    // Starfield streaming horizontally across the full screen
    for (int i = 0; i < 12; i++) {
      int x = (i * 24 + (frame * 8)) % 128;
      int y = (i * 7) % 64;
      display.drawPixel(x, y, SSD1306_WHITE);
      display.drawPixel((x + 1) % 128, y, SSD1306_WHITE); // thicker stars
    }
  } 
  else if (action == "BACKWARD_VIDEO") {
    // Fast waterfall threat lines dropping vertically down full screen
    for (int i = 0; i < 8; i++) {
      int x = (i * 18 + 5) % 128;
      int y = (frame * 10 + i * 20) % 64;
      display.drawFastVLine(x, y, 12, SSD1306_WHITE);
    }
  } 
  else if (action == "SPIN_LEFT_VIDEO") {
    // Spinning vortex lines radiating to edges
    int step = (frame * 8) % 45;
    for (int deg = step; deg < 360; deg += 45) {
      float rad = deg * 0.01745;
      display.drawLine(64, 32, 64 + cos(rad) * 70, 32 + sin(rad) * 70, SSD1306_WHITE);
    }
  } 
  else if (action == "SPIN_RIGHT_VIDEO") {
    // Opposite spinning vortex lines
    int step = (frame * 8) % 45;
    for (int deg = step; deg < 360; deg += 45) {
      float rad = (360 - deg) * 0.01745;
      display.drawLine(64, 32, 64 + cos(rad) * 70, 32 + sin(rad) * 70, SSD1306_WHITE);
    }
  } 
  else { // IDLE / RESETTING
    // Concentric calm ripples radiating out to screen bounds
    int r1 = (frame * 2) % 80;
    int r2 = ((frame * 2) + 40) % 80;
    if (r1 < 75) display.drawCircle(64, 32, r1, SSD1306_WHITE);
    if (r2 < 75) display.drawCircle(64, 32, r2, SSD1306_WHITE);
  }

  // -----------------------------------------------------------
  // LAYER 2: INTERACTIVE EYES & MATCHING EYEBROWS
  // -----------------------------------------------------------
  int leftX = 35, rightX = 93, eyeY = 34;
  bool blink = (frame % 80 < 5);
  int lookX = (sin(frame * 0.2) * 8);

  // Background cut-outs behind eyes so patterns don't bleed into pupils
  if (!blink) {
    display.fillCircle(leftX + lookX, eyeY, 16, SSD1306_BLACK);
    display.fillCircle(rightX + lookX, eyeY, 16, SSD1306_BLACK);
  } else {
    display.fillRect(leftX - 12, eyeY - 4, 24, 8, SSD1306_BLACK);
    display.fillRect(rightX - 12, eyeY - 4, 24, 8, SSD1306_BLACK);
  }

  if (!blink) {
    // --- Render Pupils ---
    display.fillCircle(leftX + lookX, eyeY, 13, SSD1306_WHITE);
    display.fillCircle(rightX + lookX, eyeY, 13, SSD1306_WHITE);
    display.fillCircle(leftX + lookX + (lookX / 2), eyeY - 2, 4, SSD1306_BLACK);
    display.fillCircle(rightX + lookX + (lookX / 2), eyeY - 2, 4, SSD1306_BLACK);

    // --- Render Matching Eyebrows ---
    if (action == "FORWARD_VIDEO") {
      // Happy / Curved upward eyebrows
      display.drawCircle(leftX + lookX, eyeY + 2, 22, SSD1306_WHITE);
      display.drawCircle(rightX + lookX, eyeY + 2, 22, SSD1306_WHITE);
      display.fillRect(0, eyeY - 14, 128, 14, SSD1306_BLACK); // Mask lower half of the circle
    } 
    else if (action == "BACKWARD_VIDEO") {
      // Angry / Inward sloping aggressive eyebrows (V-shape)
      display.drawLine(leftX - 12, eyeY - 16, leftX + 12, eyeY - 10, SSD1306_WHITE);
      display.drawLine(leftX - 12, eyeY - 15, leftX + 12, eyeY - 9,  SSD1306_WHITE); // Thicker
      
      display.drawLine(rightX - 12, eyeY - 10, rightX + 12, eyeY - 16, SSD1306_WHITE);
      display.drawLine(rightX - 12, eyeY - 9,  rightX + 12, eyeY - 15, SSD1306_WHITE); // Thicker
    } 
    else if (action == "SPIN_LEFT_VIDEO" || action == "SPIN_RIGHT_VIDEO") {
      // Confused / Worried dynamic slanting eyebrows (Left high, Right low)
      display.drawLine(leftX - 12, eyeY - 16, leftX + 12, eyeY - 13, SSD1306_WHITE);
      display.drawLine(leftX - 12, eyeY - 15, leftX + 12, eyeY - 12, SSD1306_WHITE);
      
      display.drawLine(rightX - 12, eyeY - 13, rightX + 12, eyeY - 10, SSD1306_WHITE);
      display.drawLine(rightX - 12, eyeY - 12, rightX + 12, eyeY - 9,  SSD1306_WHITE);
    } 
    else {
      // Neutral / Flat calm horizontal eyebrows sitting just above eyes
      display.drawFastHLine(leftX - 12, eyeY - 14, 24, SSD1306_WHITE);
      display.drawFastHLine(leftX - 12, eyeY - 13, 24, SSD1306_WHITE);
      
      display.drawFastHLine(rightX - 12, eyeY - 14, 24, SSD1306_WHITE);
      display.drawFastHLine(rightX - 12, eyeY - 13, 24, SSD1306_WHITE);
    }
  } 
  else {
    // Blinking State: Keep eyebrows relaxed and flat over shut lines
    display.drawLine(leftX - 12, eyeY, leftX + 12, eyeY, SSD1306_WHITE);
    display.drawLine(rightX - 12, eyeY, rightX + 12, eyeY, SSD1306_WHITE);
    display.drawFastHLine(leftX - 10, eyeY - 10, 20, SSD1306_WHITE);
    display.drawFastHLine(rightX - 10, eyeY - 10, 20, SSD1306_WHITE);
  }

  display.display();
}

// ==========================================
// 9. SETUP ROUTINE
// ==========================================
void setup() {
  Serial.begin(115200);
  SerialBT.begin("ESP32_Quadruped"); 
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  for (int i = 0; i < totalServos; i++) {
    pinMode(servoPins[i], OUTPUT);
    analogWriteFrequency(servoPins[i], 50);  
    analogWriteResolution(servoPins[i], 12); 
  }
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_PIN, INPUT);

  // Initialize Homes
  writeESP32Servo(LF_HIP_PIN, LF_Hip_Home); writeESP32Servo(LF_KNEE_PIN, LF_Knee_Home);
  writeESP32Servo(RF_HIP_PIN, RF_Hip_Home); writeESP32Servo(RF_KNEE_PIN, RF_Knee_Home);
  writeESP32Servo(LR_HIP_PIN, LR_Hip_Home); writeESP32Servo(LR_KNEE_PIN, LR_Knee_Home);
  writeESP32Servo(RR_HIP_PIN, RR_Hip_Home); writeESP32Servo(RR_KNEE_PIN, RR_Knee_Home);
  writeESP32Servo(TAIL_PIN, Tail_Home);
  
  Serial.println("[ONLINE] Engine Active with Dynamic Expressions.");
}

// ==========================================
// 10. CORE DESERIALIZATION & SCHEDULING LOOP
// ==========================================
void loop() {
  // --- A. SERIAL COMMAND PARSER ---
  if (SerialBT.available()) {
    char command = SerialBT.read();
    
    if (command != '\n' && command != '\r' && command != ' ') {
      command = toupper(command);

      // Mode Selection Interrupts
      if (command == 'Q') {
        currentOpMode = AUTO_MODE;
        currentState = WALKING_FORWARD; 
        Serial.println("[MODE] Shifted to AUTOMODE");
      } 
      else if (command == 'W') {
        currentOpMode = CONTROL_MODE;
        if (currentState != IDLE && currentState != RESETTING) {
          currentState = RESETTING;
          executeLiftAndReset(); 
        }
        Serial.println("[MODE] Shifted to CONTROL MODE");
      }
      
      // Control Mode Direction Intercepts
      if (currentOpMode == CONTROL_MODE) {
        if (command == 'F')      currentState = WALKING_FORWARD;
        else if (command == 'B') currentState = WALKING_BACKWARD;
        else if (command == 'L') currentState = TURNING_LEFT;
        else if (command == 'R') currentState = TURNING_RIGHT;
        else if (command == 'S') {
          if (currentState != IDLE && currentState != RESETTING) {
            currentState = RESETTING;
            executeLiftAndReset();
          }
        }
      }
    }
  }

  // --- B. AUTOMODE SENSOR PROCESSING ---
  if (currentOpMode == AUTO_MODE) {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    float dist = pulseIn(ECHO_PIN, HIGH, 20000) * 0.034 / 2;
    int irState = digitalRead(IR_PIN); 

    // Emergency Drop-Off Processing
    if (irState == HIGH && currentState != WALKING_BACKWARD && currentState != TURNING_LEFT) {
      currentState = WALKING_BACKWARD; 
      stateTimer = millis(); 
    }

    if (currentState == WALKING_BACKWARD) {
      if (millis() - stateTimer > 1200) { 
        currentState = TURNING_LEFT;      
        stateTimer = millis();
      }
    } 
    else if (currentState == TURNING_LEFT) {
      if (millis() - stateTimer > 800) {  
        currentState = WALKING_FORWARD;   
      }
    }
    // Proximity Matrix (15cm - 20cm reaction bounds)
    else {
      if (dist > 0 && dist < 18) {
        currentState = TURNING_RIGHT;     
      } else {
        currentState = WALKING_FORWARD;    
      }
    }
  }

  // --- C. MOTOR TASK SCHEDULER ---
  if (currentState == WALKING_FORWARD || currentState == WALKING_BACKWARD || currentState == TURNING_LEFT || currentState == TURNING_RIGHT) {
    if (millis() - lastGaitUpdate >= phaseDuration) {
      lastGaitUpdate = millis();
      executeOmniTrotGait(currentState); 
    }
  }

  // --- D. DYNAMIC DISPLAY ANIMATION SCHEDULER ---
  if (millis() - lastFrameUpdate >= 30) {
    lastFrameUpdate = millis();
    animationFrame++;
    
    if (currentState == WALKING_FORWARD) {
      playVideoAnimation("FORWARD_VIDEO", animationFrame);
    } else if (currentState == WALKING_BACKWARD) {
      playVideoAnimation("BACKWARD_VIDEO", animationFrame);
    } else if (currentState == TURNING_LEFT) {
      playVideoAnimation("SPIN_LEFT_VIDEO", animationFrame);
    } else if (currentState == TURNING_RIGHT) {
      playVideoAnimation("SPIN_RIGHT_VIDEO", animationFrame);
    } else {
      playVideoAnimation("IDLE", animationFrame);
    }
  }
}