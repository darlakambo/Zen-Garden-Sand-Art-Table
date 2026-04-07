//////////////////////////////////////////////////////////////////////////////////////////////////////
// MIE438 FINAL PROJECT - ZEN GARDEN SAND ART TABLE                                                 
//                                                                                                  
// PURPOSE:                         
// Controls a 2-motor rail gantry capable of:
// 1) Coordinate-based motion, moves requested via (x,y) coordinate location
// 2) Manual joystick motion with continuous directional control
// 3) Clock mode, autonomously draws HH:MM in the sand and redraws every 5 minutes
//
// SYSTEM OVERVIEW:
// - Mechanical arrangement uses kinematic transform calculations so cartesian motion is translated to motor stepping
// - Two stepper motors driving a belt gantry
//
// MAIN FUNCTIONS/FEATURES:
// - Serial command interface for external Python control
// - Homing sequence using limit switches to calibrate and reset carriage location
// - Motion interruption and emergency stops with limit switches
// - Position tracking with cartesian coordinates
// - Manual joystick mode for free movement
// - Clock mode using hardware RTC with periodic 5-minute redraw
//////////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////////////////////
// MODE SELECT
// Three operating modes:
// MODE_JOYSTICK   uses real time manual control with an analog joystick input.
// MODE_COORDINATE receives target XY coordinates over serial and performs automated movements.
// MODE_CLOCK      autonomously reads hardware RTC and redraws the current time every 5 minutes.
//
// Default startup mode is coordinate since the art table is designed to be primarily autonomous.
//////////////////////////////////////////////////////////////////////////////////////////////////////
#include "RTC.h"

#define MODE_JOYSTICK   1
#define MODE_COORDINATE 2
#define MODE_CLOCK      3

int currentMode = MODE_COORDINATE; //default coordinate startup mode

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ============================ COORDINATE MODE VARIABLES ==================================================
// Variables defining hardware interface for stepper motors and limit switches, and motion scaling constants
////////////////////////////////////////////////////////////////////////////////////////////////////////////
 
#define STEP_PIN1 3
#define DIR_PIN1 4
#define STEP_PIN2 6
#define DIR_PIN2 7
 
// HOME_X / HOME_Y corresponds to bottom left corner, machine origin side
#define HOME_X 10
#define HOME_Y 11

// MAX_X / MAX_Y corresponds to top right corner, farthest travel side
#define MAX_X 12
#define MAX_Y 9   
 
const int stepDelay = 1000;         // stepDelay and pulseWidth for overall motor speed
const int pulseWidth = 500;         
const int PIXEL_STEP_UNIT = 25;     // number of steps corresponding to a single grid unit
const int MAX_STEP = 600;           // maximum number of motor steps from one side to another
const int GRID_MAX = 24;            // maximum number of grid units from one side to another
const int backoffStepsY = 25;       // number of steps to back off of limit switch and end interrupt
const int backoffStepsX = 30;
 
int currentX = 0;                   // cartesian position estimate based on grid
int currentY = 0;
int currentM1 = 0;                  // motor position estimate based on steps
int currentM2 = 0;
 
bool moveInterrupted = false;       // true if motion was interrupted by switch event or stop from external communication
bool positionKnown = true;          // false if switch event invalidates position estimate
bool stopRequested = false;         // external software stop request flag
bool useLimitSwitch = true;         // allows disabling limit checks for joystick mode
 
//////////////////////////////////////////////////////////////////////////////////////////////////////
// ============================ JOYSTICK MODE VARIABLES =============================================
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
const int xPin = A0;
const int yPin = A1;
const int buttonPin = 2;
 
// deadzone for hysteresis
const int deadZone = 150;
 
// default direction is neutral at startup
String currentDirection = "Neutral";
String lastDirection = "Neutral";
 
int stepCounter = 0;
 
//////////////////////////////////////////////////////////////////////////////////////////////////////
// ============================ CLOCK MODE VARIABLES ================================================
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
volatile bool minuteElapsed = false;    // set by RTC alarm ISR every minute
bool redrawNeeded = false;              // set when 5 minutes have elapsed since last draw (cleared when draw begins)
int lastDrawnMinute = -1;               // tracks last drawn minute (-1 forces draw on first activation)
int clockHour   = 0;                    // tracks current clock hour for drawing
int clockMinute = 0;                    // tracks current clock minute for drawing
 
// Clock layout: HH:MM centred across the 24x24 grid
#define CLOCK_ROW      9    // top row of all digit cells
#define DIGIT1_COL     3    // H tens
#define DIGIT2_COL     7    // H units
#define COLON_COL     11    // colon separator
#define DIGIT3_COL    13    // M tens
#define DIGIT4_COL    17    // M units
 
//////////////////////////////////////////////////////////////////////////////////////////////////////
// ============================ SHARED LOW LEVEL FUNCTIONS ===========================================
// These are reusable low-level motion functions used by both coordinate and joystick modes.
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
void pulseStep(int stepPin) {       // generates one step pulse on selected stepper motor
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(stepDelay);
}
 
void backoffX(bool away) {          // moves gantry a short distance away from x-axis limit switch after trigger
  if (away) {
    digitalWrite(DIR_PIN1, HIGH);   // HOME_X pressed, move +X
    digitalWrite(DIR_PIN2, HIGH);
  }
  else {
    digitalWrite(DIR_PIN1, LOW);    // MAX_X pressed, move -X
    digitalWrite(DIR_PIN2, LOW);
  }
 
  // automatic backoff step for when limit switch activated
  for (int i = 0; i < backoffStepsX; i++) {   
    digitalWrite(STEP_PIN1, HIGH);  
    digitalWrite(STEP_PIN2, HIGH);  // both motors step together for straight backoff
    delayMicroseconds(pulseWidth);  
    digitalWrite(STEP_PIN1, LOW);
    digitalWrite(STEP_PIN2, LOW);   // both motors step together for straight backoff
    delayMicroseconds(stepDelay);   
  }
}
 
void backoffY(bool away) {          // moves gantry a short distance away from y-axis limit switch after trigger
  if (away) {
    digitalWrite(DIR_PIN1, LOW);    // HOME_Y pressed, move +Y
    digitalWrite(DIR_PIN2, HIGH);
  }
  else {
    digitalWrite(DIR_PIN1, HIGH);   // MAX_Y pressed, move -Y
    digitalWrite(DIR_PIN2, LOW);
  }
 
  // automatic backoff step for when limit switch activated
  for (int i = 0; i < backoffStepsY; i++) {
    digitalWrite(STEP_PIN1, HIGH);
    digitalWrite(STEP_PIN2, HIGH);  // both motors step together for straight backoff
    delayMicroseconds(pulseWidth);
    digitalWrite(STEP_PIN1, LOW);
    digitalWrite(STEP_PIN2, LOW);   // both motors step together for straight backoff
    delayMicroseconds(stepDelay);
  }
}
 
bool limitCheck() {                   // checks all limit switch readings and handles any switch event
  if (!useLimitSwitch) return false;  // if limit switch checking is disabled (joystick mode), always return false
  
  if (digitalRead(HOME_X) == LOW) {   // if HOME_X switch is triggered, back off in +X direction and invalidate position
    Serial.println("LIMIT,HOMEX");
    moveInterrupted = true;
    backoffX(true);
    positionKnown = false;
    return true;
  }
 
  if (digitalRead(MAX_X) == LOW) {    // if MAX_X switch is triggered, back off in -X direction and invalidate position
    Serial.println("LIMIT,MAXX");
    moveInterrupted = true;
    backoffX(false);
    positionKnown = false;
    return true;
  }
 
  if (digitalRead(HOME_Y) == LOW) {   // if HOME_Y switch is triggered, back off in +Y direction and invalidate position
    Serial.println("LIMIT,HOMEY");
    moveInterrupted = true;
    backoffY(true);
    positionKnown = false;
    return true;
  }
 
  if(digitalRead(MAX_Y) == LOW) {     // if MAX_Y switch is triggered, back off in -Y direction and invalidate position
    Serial.println("LIMIT,MAXY");
    moveInterrupted = true;
    backoffY(false);
    positionKnown = false;
    return true;
  }
  return false;     // otherwise return false if no limit switch event detected
}
 
void stepMotor1(bool dir) {           // steps motor 1 by one step if safe to do so
  if (limitCheck() || stopRequested) return;
 
  digitalWrite(DIR_PIN1, dir ? HIGH:LOW);   // if dir is true, set HIGH for + direction; if false, set LOW for - direction
  pulseStep(STEP_PIN1);
 
  currentM1 += (dir ? 1:-1);          // update motor position estimate based on direction stepped
 
  if (limitCheck() || stopRequested) return; // if limit reached or stop requested, set moveInterrupted and invalidate position
}
 
void stepMotor2(bool dir) {           // steps motor 2 by one step if safe to do so
  if (limitCheck() || stopRequested) return;
 
  digitalWrite(DIR_PIN2, dir ? HIGH:LOW);   // if dir is true, set HIGH for + direction; if false, set LOW for - direction
  pulseStep(STEP_PIN2);
 
  currentM2 += (dir ? 1:-1);        // update motor position estimate based on direction stepped
 
  if (limitCheck() || stopRequested) return;  // if limit reached or stop requested, set moveInterrupted and invalidate position
}
 
void stepBoth(bool dir1, bool dir2) {  // steps both motors simultaneously by one step if safe to do so
  if (limitCheck() || stopRequested) return;  // if limit reached or stop requested, set moveInterrupted and invalidate position
 
  digitalWrite(DIR_PIN1, dir1 ? HIGH:LOW);    // if dir1 is true, set HIGH for + direction; if false, set LOW for - direction
  digitalWrite(DIR_PIN2, dir2 ? HIGH:LOW);    // if dir2 is true, set HIGH for + direction; if false, set LOW for - direction
 
  digitalWrite(STEP_PIN1, HIGH);
  digitalWrite(STEP_PIN2, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(STEP_PIN1, LOW);
  digitalWrite(STEP_PIN2, LOW);  
  delayMicroseconds(stepDelay);
 
  currentM1 += (dir1 ? 1:-1);     // update motor1 position estimate based on direction stepped
  currentM2 += (dir2 ? 1:-1);     // update motor2 position estimate based on direction stepped
 
  if (limitCheck() || stopRequested) return;  // if limit reached or stop requested, set moveInterrupted and invalidate position
}
 
void homeSystem() {                       // performs homing to establish a known reference location
  Serial.println("INFO,HOMING");          // print homing status
 
  while (digitalRead(HOME_X) == HIGH) {   // moves in -X direction until limit switch is hit
    if (stopRequested) return;        // if stop requested, exit homing immediately (allows for emergency stop during homing)
    
    digitalWrite(DIR_PIN1, LOW);
    digitalWrite(DIR_PIN2, LOW);
 
    digitalWrite(STEP_PIN1, HIGH);
    digitalWrite(STEP_PIN2, HIGH);
    delayMicroseconds(pulseWidth);
    digitalWrite(STEP_PIN1, LOW);
    digitalWrite(STEP_PIN2, LOW);  
    delayMicroseconds(stepDelay);
  }
  delay(300);       // short delay to ensure stable limit switch reading
  backoffX(true);   // back off of the limit switch in X
 
  while(digitalRead(HOME_Y) == HIGH) {    // moves in -Y direction until limit switch is hit
    if (stopRequested) return;
    digitalWrite(DIR_PIN1, HIGH);
    digitalWrite(DIR_PIN2, LOW);
 
    digitalWrite(STEP_PIN1, HIGH);
    digitalWrite(STEP_PIN2, HIGH);
    delayMicroseconds(pulseWidth);
    digitalWrite(STEP_PIN1, LOW);
    digitalWrite(STEP_PIN2, LOW);  
    delayMicroseconds(stepDelay);
  }
  delay(300);
  backoffY(true);   // back off of the limit switch in Y
 
  // reset position estimates to known reference location after homing complete
  currentX = 0;
  currentY = 0;
  currentM1 = 0;
  currentM2 = 0;
  moveInterrupted = false;
  positionKnown = true;
 
  Serial.println("OK,HOME");      // print homing complete status and current position
  Serial.println("POS,0,0");      // position known with coordinates (0,0) at home position
}
 
bool motorMoveSafe(long M1Steps, long M2Steps) {     // predicts motor movement based on steps
  long targetM1 = currentM1 + M1Steps;
  long targetM2 = currentM2 + M2Steps;
 
  // checks if predicted motor position is within bounds, otherwise returns false to prevent unsafe movement
  if (targetM1 < 0 || targetM1 > MAX_STEP) return false;
  if (targetM2 < 0 || targetM2 > MAX_STEP) return false;
 
  return true;    // if predicted motor position is within bounds, return true to allow movement
}
 
void moveMotors(long M1Steps, long M2Steps) {        // coordinates motion function, desired relative movement of motors
  moveInterrupted = false;
  stopRequested = false;
 
  // calculates absolute step values and directions for each motor
  long absM1 = abs(M1Steps);
  long absM2 = abs(M2Steps);
 
  //set directions equal to motor steps if positive, otherwise set to false for negative (used for stepping functions)
  bool dir1 = (M1Steps >= 0);
  bool dir2 = (M2Steps >= 0);
 
  long maxSteps = max(absM1, absM2);    // total number of steps for longest motor movement, used to calculate step ratio for coordinated motion
 
  // accumulators for motor movements
  long acc1 = 0;   
  long acc2 = 0;
 
  for (long i = 0; i < maxSteps; i++) {   // loop for total number of steps in longest motor movement
    if (stopRequested) {              // if stop requested from external communication, set
      moveInterrupted = true;
      Serial.println("ERR,STOPPED");
      return;
    }
 
    // determine if each motor should step on this iteration
    bool moveM1 = false;    
    bool moveM2 = false;
 
    // increment accumulators by absolute motor steps
    acc1 += absM1;
    acc2 += absM2;
 
    if (acc1 >= maxSteps) {   // if accumulator exceeds maxSteps, step that motor to step and reduce accumulator by maxSteps
      moveM1 = true;
      acc1 -= maxSteps;
    }
 
    if (acc2 >= maxSteps) {   // if accumulator exceeds maxSteps, step that motor to step and reduce accumulator by maxSteps
      moveM2 = true;
      acc2 -= maxSteps;
    }
 
    if (moveM1 && moveM2) {   // if both motors need to step on this iteration, step together for coordinated motion
      stepBoth(dir1, dir2);
    }
    else if (moveM1) {        // if only motor 1 needs to step, step motor 1
      stepMotor1(dir1);
    }
    else if (moveM2) {        // if only motor 2 needs to step, step motor 2
      stepMotor2(dir2);
    }
    if (moveInterrupted) return;    // if movement was interrupted by limit switch or stop request, exit movement
  }
}
 
void moveBy(int xPixels, int yPixels) {              // converts between pixels and motor steps
  int targetX = currentX + xPixels;
  int targetY = currentY + yPixels;
 
  if (targetX < 0 || targetX > GRID_MAX || targetY < 0 || targetY > GRID_MAX) {   // if target coordinate is out of bounds, print error and exit movement
    Serial.println("ERR,OUT_OF_BOUNDS");
    return;
  }
  
  // set motor units based on pixel calculations from kinematic transform
  long m1Units = xPixels - yPixels;
  long m2Units = xPixels + yPixels;
 
  // set motor steps based on motor units and scaling constant
  long m1Steps = m1Units * PIXEL_STEP_UNIT;
  long m2Steps = m2Units * PIXEL_STEP_UNIT;
 
  int prevM1 = currentM1;   // save current motor positions before movement
  int prevM2 = currentM2;
 
  moveMotors(m1Steps, m2Steps);
 
  // updating XY and motor positions
  if (!moveInterrupted && currentM1 == prevM1 + m1Steps && currentM2 == prevM2 + m2Steps) {
    currentX = targetX;
    currentY = targetY;
    
    Serial.print("OK,MOVE,");
    Serial.print(currentX);
    Serial.print(",");
    Serial.print(currentY);
    Serial.println("");
 
    // print new position
    Serial.print("POS,");
    Serial.print(currentX);
    Serial.print(",");
    Serial.println(currentY); 
  }
  // if movement was interrupted by limit switch or stop request, print error and position unknown status
  else {
    Serial.println("ERR,MOVE_INTERRUPTED");
    Serial.println("ERR,POSITION_UNKNOWN");
  }
}
 
void moveTo(int targetX, int targetY) {              // absolute motion command to target coordinate
  if(!positionKnown) {                          // if position is not known print error and exit movement
    Serial.println("ERR,POSITION_UNKNOWN");
    return;
  }
  
  if (targetX < 0 || targetX > GRID_MAX || targetY < 0 || targetY > GRID_MAX) {       // if target coordinate is out of bounds, print error and exit movement
    Serial.println("ERR,OUT_OF_BOUNDS");
    return;
  }
  
  // otherwise calculate relative pixel movement from current position to target position
  int dx = targetX - currentX;
  int dy = targetY - currentY;
 
  moveBy(dx,dy);    // call moveBy to perform relative movement to target coordinate
}
 
//////////////////////////////////////////////////////////////////////////////////////////////////////
// ============================ JOYSTICK MODE FUNCTIONS =============================================
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
void joystickMode() {
  const int stepDelay = 1000;         // stepDelay for overall motor speed  (if joystick mode needed to be controlled slower or faster)
  useLimitSwitch = false;             // disable limit switch in joystick mode (manual control)
  
  // read joystick analog value
  int xValue = analogRead(xPin);
  int yValue = analogRead(yPin);
 
  // shift joystick values so center = 0
  int xCentered = xValue - 512;
  int yCentered = yValue - 512;
 
  // DEADZONE/HYSTERESIS (Prevents noise near center from causing movement)
  if (abs(xCentered) < deadZone && abs(yCentered) < deadZone) {
    currentDirection = "Neutral";
  } 
  else {
    // Angle Calculation 
    float angle = atan2(yCentered, xCentered) * 180.0 / PI;     // atan2 gives angle of joystick vector (in degrees)
    if (angle < 0) angle += 360;                                // Convert from [-180,180] to [0,360]
 
    // Convert continuous angle into discrete motion zones
    // right:
    if (angle >= 348.75 || angle < 22.5) currentDirection = "Right";

    // upper right quadrant (30,45,60 degrees):
    else if (angle < 37.5) currentDirection = "UpRight30";
    else if (angle < 52.5) currentDirection = "UpRight45";
    else if (angle < 67.5) currentDirection = "UpRight60";

    // up:
    else if (angle < 112.5) currentDirection = "Up";

    // upper left quadrant:
    else if (angle < 127.5) currentDirection = "UpLeft60";
    else if (angle < 142.5) currentDirection = "UpLeft45";
    else if (angle < 157.5) currentDirection = "UpLeft30";

    // left:
    else if (angle < 202.5) currentDirection = "Left";

    // lower left quadrant:
    else if (angle < 217.5) currentDirection = "DownLeft30";
    else if (angle < 232.5) currentDirection = "DownLeft45";
    else if (angle < 247.5) currentDirection = "DownLeft60";

    // down:
    else if (angle < 297.5) currentDirection = "Down";

    // lower right quadrant:
    else if (angle < 307.5) currentDirection = "DownRight60";
    else if (angle < 322.5) currentDirection = "DownRight45";
    else currentDirection = "DownRight30";
  }
 
  if (currentDirection != lastDirection) {    // if direction has changed since last loop, print new direction
    Serial.println(currentDirection);
    lastDirection = currentDirection;
  }
 
  if (currentDirection == "Neutral") return;  // if in neutral/no direction change, do not step motors

  // Check if current position is at canvas edge
  //M1 = (x - y)*PIXEL_STEP_UNIT (from before in moveBy())
  //M2 = (x + y)*PIXEL_STEP_UNIT (from before in moveBy())
  //   => M1 + M2 = (x - y)*PIXEL_STEP_UNIT + (x + y)*PIXEL_STEP_UNIT 
  //      M1 + M2 = x*PIXEL_STEP_UNIT - y*PIXEL_STEP_UNIT + x*PIXEL_STEP_UNIT + y*PIXEL_STEP_UNIT 
  //      M1 + M2 = 2*x*PIXEL_STEP_UNIT 
  //        =>  x = (M1 + M2) / (2*PIXEL_STEP_UNIT)
  //            y = (M2 - M1) / (2*PIXEL_STEP_UNIT)

  int currentCanvasX = (float)(currentM1 + currentM2) / (2.0 * PIXEL_STEP_UNIT);
  int currentCanvasY = (float)(currentM2 - currentM1) / (2.0 * PIXEL_STEP_UNIT);

  // if at left edge and trying to move more left, do not move
  if (currentCanvasX <= 0 && (currentDirection == "Left" || currentDirection.startsWith("UpLeft") || currentDirection.startsWith("DownLeft"))) 
    return;
  // if at right edge and trying to move more right, do not move
  if (currentCanvasX >= GRID_MAX && (currentDirection == "Right" || currentDirection.startsWith("UpRight") || currentDirection.startsWith("DownRight"))) 
    return;
  // if at bottom edge and trying to move more down, do not move
  if (currentCanvasY <= 0 && (currentDirection == "Down" || currentDirection.startsWith("DownLeft") || currentDirection.startsWith("DownRight"))) 
    return;
  // if at top edge and trying to move more up, do not move
  if (currentCanvasY >= GRID_MAX && (currentDirection == "Up" || currentDirection.startsWith("UpLeft") || currentDirection.startsWith("UpRight"))) 
    return;

  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  //================================Motion control based on  direction zones==============================
  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  // STRAIGHT
  if (currentDirection == "Right") stepBoth(true, true);
  else if (currentDirection == "Left") stepBoth(false, false);
  else if (currentDirection == "Up") stepBoth(false, true);
  else if (currentDirection == "Down") stepBoth(true, false);
 
  // 45deg Diagonal (only one motor moves)
  else if (currentDirection == "UpRight45") stepMotor2(true);
  else if (currentDirection == "UpLeft45") stepMotor1(false);
  else if (currentDirection == "DownRight45") stepMotor1(true);
  else if (currentDirection == "DownLeft45") stepMotor2(false);
 
  // 30deg — more horizontal: primary motor sets main axis, secondary adjusts every 2nd step
  // Grid kinematics: M1 step → dx=+0.5,dy=-0.5  M2 step → dx=+0.5,dy=+0.5
  else if (currentDirection == "UpRight30") {        // (+1.5, +0.5) per 2 steps
    stepMotor2(true);
    if (stepCounter % 2 == 0) stepMotor1(true);
  }
  else if (currentDirection == "UpLeft30") {          // (-1.5, +0.5) per 2 steps
    stepMotor1(false);
    if (stepCounter % 2 == 0) stepMotor2(false);
  }
  else if (currentDirection == "DownLeft30") {        // (-1.5, -0.5) per 2 steps
    stepMotor2(false);
    if (stepCounter % 2 == 0) stepMotor1(false);
  }
  else if (currentDirection == "DownRight30") {       // (+1.5, -0.5) per 2 steps
    stepMotor1(true);
    if (stepCounter % 2 == 0) stepMotor2(true);
  }

  // 60deg — more vertical: primary motor sets main axis, secondary adjusts every 2nd step
  else if (currentDirection == "UpRight60") {         // (+0.5, +1.5) per 2 steps
    stepMotor2(true);
    if (stepCounter % 2 == 0) stepMotor1(false);
  }
  else if (currentDirection == "UpLeft60") {          // (-0.5, +1.5) per 2 steps
    stepMotor1(false);
    if (stepCounter % 2 == 0) stepMotor2(true);
  }
  else if (currentDirection == "DownLeft60") {        // (-0.5, -1.5) per 2 steps
    stepMotor2(false);
    if (stepCounter % 2 == 0) stepMotor1(true);
  }
  else if (currentDirection == "DownRight60") {       // (+0.5, -1.5) per 2 steps
    stepMotor1(true);
    if (stepCounter % 2 == 0) stepMotor2(false);
  }
 
  stepCounter++;    // increment step counter for timing of 30/60 degree steps

  // round canvas position for GUI display and print 
  Serial.print("POS,");
  Serial.print((int)(round(currentCanvasX)));
  Serial.print(",");
  Serial.println((int)(round(currentCanvasY)));

  delay(5);        // delay GUI drawing to allow user time to respond
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
//===================================== CLOCK MODE FUNCTIONS =========================================
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
void clockAlarmISR() {          // RTC alarm interrupt service routine, called every minute when alarm triggers
  minuteElapsed = true;         // set flag to indicate minute has elapsed
  clockMinute++;                // increment clock minute variable to track time for redraw timing
  
  if (clockMinute >= 60) {      // if 60 minutes have elapsed, reset minute counter and increment hour counter
    clockMinute = 0;
    clockHour++;
    if (clockHour >= 24) clockHour = 0;     // if 24 hours have elapsed, reset hour counter to 0
  }
}
 
void eraseCanvas() {                // wipes sand with a full boustrophedon raster scan
  Serial.println("INFO,ERASING");   // print erasing status
  useLimitSwitch = true;            // enable limit switch checking (not in joystick mode)
 
  for (int row = 0; row <= GRID_MAX; row++) {     // loop through each row of the grid
    if (stopRequested || moveInterrupted) return;   // if stop requested or movement interrupted, exit erasing immediately (allows for emergency stop during erasing)
 
    // even rows go left to right
    if (row % 2 == 0) {           
      moveTo(0, row);
      moveTo(GRID_MAX, row);
    }
    // odd rows go right to left
    else {
      moveTo(GRID_MAX, row);
      moveTo(0, row);
    }
  }
}
 
// Define series of points for each digit, with moveTo commands connecting the points to form the shape of the digit.
// "0"
void drawDigit0(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
  moveTo(ox,   oy);   moveTo(ox,   oy+5);  // left side
}

// "1"
void drawDigit1(int ox, int oy) {
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side only
}

// "2"
void drawDigit2(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+3);  // lower right side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox,   oy+3); moveTo(ox,   oy+5);  // upper left side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
}

// "3"
void drawDigit3(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
}

// "4"
void drawDigit4(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox,   oy+3);  // upper left side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
}

// "5"
void drawDigit5(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox,   oy);   moveTo(ox,   oy+3);  // lower left side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox+3, oy+3); moveTo(ox+3, oy+5);  // upper right side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
}

// "6"
void drawDigit6(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox,   oy);   moveTo(ox,   oy+5);  // left side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox+3, oy+3); moveTo(ox+3, oy+5);  // upper right side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
}
 
// "7"
void drawDigit7(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
}
 
// "8"
void drawDigit8(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
  moveTo(ox,   oy);   moveTo(ox,   oy+5);  // left side
}
 
// "9"
void drawDigit9(int ox, int oy) {
  moveTo(ox,   oy);   moveTo(ox+3, oy);    // bottom side
  moveTo(ox+3, oy);   moveTo(ox+3, oy+5);  // right side
  moveTo(ox,   oy+3); moveTo(ox+3, oy+3);  // middle side
  moveTo(ox,   oy);   moveTo(ox,   oy+3);  // lower left side
  moveTo(ox,   oy+5); moveTo(ox+3, oy+5);  // top side
}
 
// Call the appropriate digit drawing sequence based on the input digit (0-9)
void drawDigit(int digit, int ox, int oy) {   
  if (stopRequested || moveInterrupted) return;       // if stop requested or movement interrupted, exit digit drawing immediately (allows for emergency stop during drawing)
  if      (digit == 0) drawDigit0(ox, oy);
  else if (digit == 1) drawDigit1(ox, oy);
  else if (digit == 2) drawDigit2(ox, oy);
  else if (digit == 3) drawDigit3(ox, oy);
  else if (digit == 4) drawDigit4(ox, oy);
  else if (digit == 5) drawDigit5(ox, oy);
  else if (digit == 6) drawDigit6(ox, oy);
  else if (digit == 7) drawDigit7(ox, oy);
  else if (digit == 8) drawDigit8(ox, oy);
  else if (digit == 9) drawDigit9(ox, oy);
}
 
void drawClockFace(int hour, int minute) {    // erases sand and draws HH:MM in 24-hour format
  Serial.println("INFO,DRAWING_CLOCK");
  useLimitSwitch = true;                      // enable limit switch checking (not in joystick mode)
 
  eraseCanvas();                              // force clean canvas for clock drawing
  if (stopRequested || moveInterrupted) {     // if stop requested or movement interrupted during erasing, exit drawing immediately and print error
    Serial.println("ERR,CLOCK_INTERRUPTED"); 
    return;
  }
 
  // decompose time into individual digits using integer division
  int d1 = hour   / 10;       // hour tens
  int d2 = hour   % 10;       // hour ones
  int d3 = minute / 10;       // minute tens
  int d4 = minute % 10;       // minute ones
 
  drawDigit(d1, DIGIT1_COL, CLOCK_ROW);         // draw digit one
  if (stopRequested || moveInterrupted) return; // check for stop or interrupt
 
  drawDigit(d2, DIGIT2_COL, CLOCK_ROW);         // draw digit two
  if (stopRequested || moveInterrupted) return; // check for stop or interrupt
 
  // colon: two small squares so each dot leaves a visible mark in the sand
  moveTo(COLON_COL,     CLOCK_ROW + 1);         
  moveTo(COLON_COL + 1, CLOCK_ROW + 1);
  moveTo(COLON_COL + 1, CLOCK_ROW + 2);
  moveTo(COLON_COL,     CLOCK_ROW + 2);         // upper dot
  if (stopRequested || moveInterrupted) return; // check for stop or interrupt
 
  moveTo(COLON_COL,     CLOCK_ROW + 3);
  moveTo(COLON_COL + 1, CLOCK_ROW + 3);
  moveTo(COLON_COL + 1, CLOCK_ROW + 4);
  moveTo(COLON_COL,     CLOCK_ROW + 4);         // lower dot
  if (stopRequested || moveInterrupted) return; // check for stop or interrupt
 
  drawDigit(d3, DIGIT3_COL, CLOCK_ROW);         // draw digit three
  if (stopRequested || moveInterrupted) return; // check for stop or interrupt
 
  drawDigit(d4, DIGIT4_COL, CLOCK_ROW);         // draw digit four
 
  Serial.println("OK,CLOCK_DRAWN");             // print clock drawn status
  Serial.print("POS,");                         // print current position after drawing clock
  Serial.print(currentX);
  Serial.print(",");
  Serial.println(currentY);
}
 
void clockMode() {
  // clock mode main loop, checks for minute elapsed flag to determine when to update clock drawing
    if (minuteElapsed) {            // if minuteElapsed flag set by RTC alarm ISR, proceed with clock update
        __disable_irq();            // disable interrupts while checking and resetting minuteElapsed flag to
        minuteElapsed = false;      // reset minuteElapsed flag for next minute
        __enable_irq();             // re-enable interrupts after resetting flag

        int elapsed = (clockMinute - lastDrawnMinute + 60) % 60;        // calculate elapsed minutes since last clock drawing,(minutes wraparound at 60)
        if (lastDrawnMinute == -1 || elapsed >= 5) {            // if first time drawing clock (lastDrawnMinute == -1) or 5 or more minutes have elapsed since last drawing, set redrawNeeded flag to update clock face
            redrawNeeded    = true;
            lastDrawnMinute = clockMinute;
        }
    }

    if (redrawNeeded) {                           // if redrawNeeded flag set, call drawClockFace to update clock drawing with current time
        redrawNeeded = false;
        drawClockFace(clockHour, clockMinute);
    }
}
 
//////////////////////////////////////////////////////////////////////////////////////////////////////
// =========================================== MAIN ==================================================
// Implements external serial interface.
//////////////////////////////////////////////////////////////////////////////////////////////////////
 
void sendStatus() {                     // sends current machine position and mode to host
  Serial.print("STATUS,");
  Serial.print(positionKnown ? "KNOWN" : "UNKNOWN");    // if positionKnown is true, print KNOWN; if false, print UNKNOWN
  Serial.print(",");
  Serial.print(currentX);               // print current X coordinate
  Serial.print(",");
  Serial.print(currentY);               // print current Y coordinate
  Serial.print(",");
  if      (currentMode == MODE_JOYSTICK)   Serial.println("JOYSTICK");        // if currentMode is MODE_JOYSTICK, print JOYSTICK
  else if (currentMode == MODE_CLOCK)      Serial.println("CLOCK");           // if currentMode is MODE_CLOCK, print CLOCK
  else                                     Serial.println("COORDINATE");      // otherwise currentMode is MODE_COORDINATE, print COORDINATE
}
 
void setMode(int mode) {                // switches between joystick, coordinate, and clock control modes
  currentMode = mode;         // update currentMode variable to new mode
  stopRequested = false;      // clear any stop request when changing modes
 
  if (currentMode == MODE_JOYSTICK) {     // if new mode is joystick mode, print mode change status
    Serial.println("OK,MODE,JOYSTICK");
  }
  else if (currentMode == MODE_CLOCK) {   // if new mode is clock mode, reset clock drawing variables and print mode change status
    lastDrawnMinute = -1;               // force 5-minute check to trigger on first activation
    redrawNeeded    = false;            // clear any leftover flag from a previous activation
    Serial.println("OK,MODE,CLOCK");
  }
  else {                                  // otherwise new mode is coordinate mode, print mode change status
    Serial.println("OK,MODE,COORDINATE");
  }
}
 
void processCommand(String input) {     // parses incoming serial commands from python controller
  input.trim();             // remove any leading/trailing whitespace from the input command
  input.toUpperCase();      // convert input command to uppercase for case-insensitive command parsing
 
  if (input == "HOME") {    // if command is HOME, call homeSystem
    homeSystem();
    return;
  }
 
  if (input == "GETPOS") {  // if command is GETPOS, return current position (or unknown status if positionKnown is false)
    Serial.print("POS,");
    Serial.print(currentX);
    Serial.print(",");
    Serial.println(currentY);
    return;
  }
 
  if (input == "STATUS") {  // if command is STATUS, call sendStatus
    sendStatus();
    return;
  }
 
  if (input == "STOP") {    // if command is STOP, set stopRequested to true and print stop status
    stopRequested = true;
    Serial.println("OK,STOP");
    return;
  }
 
  if (input == "SETMODE,JOYSTICK") {    // if command is SETMODE,JOYSTICK, switch to joystick mode
    setMode(MODE_JOYSTICK);
    return;
  }
 
  if (input == "SETMODE,COORDINATE") {  // if command is SETMODE,COORDINATE, switch to coordinate mode
    setMode(MODE_COORDINATE);
    return;
  }
 
  if (input == "SETMODE,CLOCK") {       // if command is SETMODE,CLOCK, switch to clock mode
    setMode(MODE_CLOCK);
    return;
  }
 
  if (input == "ERASE") {               // if command is ERASE, call eraseCanvas to perform a full canvas wipe
    eraseCanvas();                              // call eraseCanvas to clear sandbox
    if (!stopRequested && !moveInterrupted) {   // if erasing completed without stop request or movement interruption, print erase complete status and current position
      Serial.println("OK,ERASE");
      Serial.print("POS,");
      Serial.print(currentX);
      Serial.print(",");
      Serial.println(currentY);
    }
    return;
  }
 
  if (input.startsWith("SETTIME,")) {   // if command starts with SETTIME, parse time values and set RTC time accordingly
    // expected format: SETTIME,hour,minute 
    int c1 = input.indexOf(',');            
    int c2 = input.indexOf(',', c1 + 1);

    if (c2 > c1) {                                          //if two commas found, parse hour and minute values from command and set RTC time
        clockHour   = input.substring(c1 + 1, c2).toInt();
        clockMinute = input.substring(c2 + 1).toInt();
        Serial.println("OK,TIME_SET");
    } else {                                                // otherwise print error for bad SETTIME command
        Serial.println("ERR,BAD_SETTIME");
    }
    return;
  }
 
  if (input.startsWith("MOVE,")) {
    if (currentMode != MODE_COORDINATE) {
      Serial.println("ERR,WRONG_MODE");
      return;
    }
    int comma1 = input.indexOf(',');
    int comma2 = input.indexOf(',', comma1 + 1);
 
    if (comma2 > comma1) {
      int x = input.substring(comma1 + 1, comma2).toInt();
      int y = input.substring(comma2 + 1).toInt();
      moveTo(x,y);
      return;
    }
  }
  Serial.println("ERR,BAD_COMMAND");
}
 
void setup() {                          // hardware initialization and startup
  pinMode(STEP_PIN1, OUTPUT);
  pinMode(DIR_PIN1, OUTPUT);
  pinMode(STEP_PIN2, OUTPUT);
  pinMode(DIR_PIN2, OUTPUT);
 
  pinMode(HOME_X, INPUT_PULLUP);
  pinMode(HOME_Y, INPUT_PULLUP);
  pinMode(MAX_X, INPUT_PULLUP);
  pinMode(MAX_Y, INPUT_PULLUP);
 
  pinMode(buttonPin, INPUT_PULLUP);
  
  Serial.begin(115200);
  delay(2000);
 
  // RTC setup -- set a default time; Python controller can override with SETTIME command
  RTC.begin();
  RTCTime startTime(1, Month::JANUARY, 2025, 0, 0, 0, DayOfWeek::WEDNESDAY, SaveLight::SAVING_TIME_ACTIVE);
  RTC.setTime(startTime);
 
  // RTC alarm fires every minute at :00 seconds
  // ISR sets minuteElapsed flag; clockMode() in loop() checks the 5-minute interval
  RTCTime alarmTime;
  alarmTime.setSecond(0);
  AlarmMatch matchRule;
  matchRule.addMatchSecond();
  RTC.setAlarmCallback(clockAlarmISR, alarmTime, matchRule);
 
  Serial.println("READY");
  Serial.println("MODE,COORDINATE");
}
 
void loop() {                           // main control loop, checks for incoming serial commands or joystick/clock mode
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    processCommand(input);
  }
  
  if (currentMode == MODE_JOYSTICK) {
    joystickMode();
  }
 
  if (currentMode == MODE_CLOCK) {
    clockMode();
  }
}