/*
  TannFirmware - Pause water flow on your water tap by touching it.
  Project URL: https://github.com/ivanseidel/Tann

  The main purpose of this code is to:
    - Keep track of water flow with the Sensor
    - Keep track of capacitive Sensor
    - Keep track of water spent, and 'saved'
    - Send a byte with the amount of water saved every X Days (Send to ESP8266)

  There are 3 Main states:
    - IDDLE:
      Valve is Open, and Waitig for flow to happen
      ~(touch happens) goto OPENING
      ~(flow happens)  goto OPEN

    - OPENING:
      Valve is Open, water is flowing, and user IS touching
      ~(touch stops)   goto OPEN

    - OPEN:
      Valve is Open, water is flowing, and user is NOT touching
      ~(no flow)       goto IDDLE

    - PAUSED:
      Valve is Closed, water is not flowing, and user IS touching
      ~(touch stops)   goto OPEN

*/

#define PIN_SENSOR_FLOW         2
#define PIN_SENSOR_TOUCH        A0
#define PIN_VALVE               6
int TOUCH_THRESHOLD = 422;

#define DEBOUNCE_TOUCH_START      0
#define DEBOUNCE_TOUCH_END        200
#define DEBOUNCE_FLOW_STOP        300
#define FLOW_SENSOR_THRESHOLD     50

#define VALVE_OPEN              true
#define VALVE_CLOSED            false

//
// Reads the capacitive touch sensor
//
bool touchSensorValue = 0;
float touchSensorAvg = TOUCH_THRESHOLD;
void readTouchSensor(float weight = 0.03){
  int sensorValue = analogRead(PIN_SENSOR_TOUCH);

  touchSensorAvg += (sensorValue - touchSensorAvg) * weight;

  if(touchSensorAvg > TOUCH_THRESHOLD)
    touchSensorValue = true;
  else
    touchSensorValue = false;

  // Serial.print(touchSensorValue);
  // Serial.print("\t");
  // Serial.println(touchSensorAvg);
}

//
// Reads the flow value (With interruption)
//
int flowSensorValue = 0;
bool flowSensorHasFlow = 0;

// count how many pulses!
volatile uint16_t pulses = 0;
// track the state of the pulse pin
volatile uint8_t lastflowpinstate;
// you can try to keep time of how long it is between pulses
volatile uint32_t lastflowratetimer = 0;
// and use that to calculate a flow rate
volatile float flowrate;
// Interrupt is called once a millisecond, looks for any pulses from the sensor!
SIGNAL(TIMER0_COMPA_vect) {
  uint8_t x = digitalRead(PIN_SENSOR_FLOW);

  if (x == lastflowpinstate) {
    lastflowratetimer++;
    return; // nothing changed!
  }

  if (x == HIGH) {
    //low to high transition!
    pulses++;
  }
  lastflowpinstate = x;
  flowrate = 1000.0;
  flowrate /= lastflowratetimer;  // in hertz
  lastflowratetimer = 0;

  // Set boolean flag
  flowSensorHasFlow = flowrate > FLOW_SENSOR_THRESHOLD && flowrate < 5000;
}

void initFlowSensor(boolean v) {
  if (v) {
    // Timer0 is already used for millis() - we'll just interrupt somewhere
    // in the middle and call the "Compare A" function above
    OCR0A = 0xAF;
    TIMSK0 |= _BV(OCIE0A);
  } else {
    // do not call the interrupt function COMPA anymore
    TIMSK0 &= ~_BV(OCIE0A);
  }
}

//
// Sets the output for the solenoid valve
//
bool valveState = 1;
void setValveOutput(bool open){
  valveState = open;
  digitalWrite(PIN_VALVE, valveState);
}

#define STYLE_TOUCH_PAUSE         0
#define STYLE_TOUCH_INVERT_PAUSE  1
#define STYLE_TOUCH_TIMER         2

#define STATE_IDDLE               0
#define STATE_OPENING             1
#define STATE_OPEN                2
#define STATE_PAUSED              3

int currentState = STATE_IDDLE;

void stateIddle(){
  static bool lastTouchSensorValue = 0;
  static unsigned long lastTouch = 0;

  setValveOutput(VALVE_OPEN);

  // Debounce Touch
  if(touchSensorValue != lastTouchSensorValue && touchSensorValue)
    lastTouch = millis();

  // Save last state
  lastTouchSensorValue = touchSensorValue;

  // Check if spent more than DEBOUNCE_TOUCH_START
  if(touchSensorValue && millis() - lastTouch > DEBOUNCE_TOUCH_START)
    currentState = STATE_OPENING;

  // Check flux
  if(flowSensorValue > FLOW_SENSOR_THRESHOLD)
    currentState = STATE_OPEN;
}

void stateOpening(){
  static unsigned long lastTouch = 0;

  // Bypass
  currentState = STATE_OPEN;
  return;

  setValveOutput(VALVE_OPEN);

  if(touchSensorValue)
    lastTouch = millis();

  // Check if released for more than debounce
  if(!touchSensorValue && millis() - lastTouch > DEBOUNCE_TOUCH_END)
    currentState = STATE_OPEN;
}

void stateOpen(){
  static unsigned long lastFlow = 0;
  static unsigned long lastNoTouch = 0;

  setValveOutput(VALVE_OPEN);

  if(flowSensorHasFlow)
    lastFlow = millis();

  if(!flowSensorHasFlow && millis() - lastFlow > DEBOUNCE_FLOW_STOP)
    currentState = STATE_IDDLE;

  if(!touchSensorValue)
    lastNoTouch = millis();

  if(touchSensorValue && millis() - lastNoTouch > DEBOUNCE_TOUCH_START)
    currentState = STATE_PAUSED;
}

void statePaused(){
  static unsigned long lastTouch = 0;

  setValveOutput(VALVE_CLOSED);

  if(touchSensorValue)
    lastTouch = millis();

  // Check if released for more than debounce
  if(!touchSensorValue && millis() - lastTouch > DEBOUNCE_TOUCH_END)
    currentState = STATE_OPEN;
}

//
// Setup
//
void setup(){
  // Start serial
  Serial.begin(57600);

  // Output
  pinMode(PIN_VALVE, OUTPUT);

  // Input
  pinMode(PIN_SENSOR_FLOW, INPUT);
  pinMode(PIN_SENSOR_TOUCH, INPUT);

  // Init flow sensor
  initFlowSensor(true);

  // Calibrate sensor
  // delay(1000);
  // touchSensorAvg = analogRead(PIN_SENSOR_TOUCH);
  // float max = 0;
  // float min = 1023;
  //
  // while(millis() < 2000){
  //   readTouchSensor(0.01);
  // }
  //
  // while(millis() < 8000){
  //   if(max < touchSensorAvg)
  //     max = touchSensorAvg;
  //   if(min > touchSensorAvg)
  //     min = touchSensorAvg;
  //     // delay(1);
  // }
  // TOUCH_THRESHOLD = (max + min) / 2.0;
}

//
// Loop
//
void loop(){
  readTouchSensor();
  // readFluxSensor();
  //
  Serial.print(currentState);
  Serial.print("\ttouch ");
  Serial.print(touchSensorValue);
  Serial.print("\t");
  Serial.print(touchSensorAvg);
  Serial.print("\tflow ");
  Serial.print(flowSensorHasFlow);
  Serial.print("\tvalve ");
  Serial.println(valveState);

  if(currentState == STATE_IDDLE)
    stateIddle();
  else if(currentState == STATE_OPENING)
    stateOpening();
  else if(currentState == STATE_OPEN)
    stateOpen();
  else if(currentState == STATE_PAUSED)
    statePaused();
  else
    currentState = STATE_IDDLE;

  delay(1);
}
