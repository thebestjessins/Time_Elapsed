/*********************

Example code for the Adafruit b/w Character LCD Shield and Library

This code displays text on the shield, and also reads the buttons on the keypad.
When a button is pressed, the backlight changes color.

Build to change time and speed calculations
**********************/

// include the library code:
#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <EEPROM.h> 


// The shield uses the I2C SCL and SDA pins. On classic Arduinos
// this is Analog 4 and 5 so you can't use those for analogRead() anymore
// However, you can connect other I2C sensors to the I2C bus and share
// the I2C bus.
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

// These #defines make it easy to set the backlight color
#define OFF 0x0
#define ON 0x1


unsigned long debounce = 0; //delay to keep button inputs from going to fast
// menu auto return is now debounce + 2700
//unsigned long menuAutoReturn = 0; //timer to return to main speedo display from menu
unsigned long timerStartMPH = 0; //starting timer mph
unsigned long timerStopMPH = 30; //ending timer mph

/* mmenu text loop and position and depth 
 * 
 */
byte menuPosition = 0;
byte menuLevel = 0;

int speedMain = 0; // main speedo current speed, filled in from interupt trigger
int speedMax = 0;
int oldSpeed = 0; // to compare current speed to for updating printout 
int currentSpeed = 0; //runing current speed, set from main, called from anything
//int wheelDiameter = 556; //in mm
unsigned long diaCalc;

unsigned long tempFinalTime = 0; // elapsed time stopped time, stored in history

//volatile variables for interupt time pickup
volatile byte triggerTimerState, triggerStartFlag; //stores condition of the timer
volatile unsigned long triggerTimeDelta=3089, triggerTimeLast=0;
volatile unsigned long timerTriggerButtonInput_Debounce = 0;


//for speedometer calculations
unsigned long triggerTimerElapsed;
unsigned long timerStop = 130; //precalculated value of timer for 556mm wheels at 30mph
unsigned long timerStart = 0; 

//stored time values 
unsigned long storedTimeArray[10]={0,0,0,0,0,0,0,0,0,0};
byte storedTimeWrite = 0;



void setup() {
  // Debugging output
  Serial.begin(9600);
  // set up the LCD's number of columns and rows: 
  lcd.begin(16, 2);

  // Print a message to the LCD. We track how long it takes since
  // this library has been optimized a bit and we're proud of it :)

  lcd.setCursor(0,0);
  lcd.print("Speed:     0 mph");
  lcd.setCursor(0,1);
  lcd.print("Time:  0.000 sec");

  lcd.setBacklight(ON);

  // bring info from EEPROM
  diaCalc = EEPROMReadInt(1)*70272;
  speedMax = EEPROM.read(3);
  timerStartMPH = EEPROM.read(4);
  timerStopMPH = EEPROM.read(5);
  if (timerStartMPH == 0) 
    timerStart = 0;
  else
    timerStart = diaCalc/(timerStartMPH*10000);
  if (timerStopMPH == 0) 
    timerStop = 0;
  else
    timerStop = diaCalc/(timerStopMPH*10000);  

  //hall trigger for speedo pickup trigger
  attachInterrupt(digitalPinToInterrupt(3), speedoPickup, RISING);
  attachInterrupt(digitalPinToInterrupt(2), timerTriggerButtonInput, RISING);
}


/*
 Functions to deal with EEPROM storing and recalling
*/


/*
 * EEPROM only can store one byte to each location and they must be remembered manually 
 * Position 1,2 are used to store wheel diameter
 * Position 3,4 are used to store top speed value
 * Position 4,5 are for time start and stop speed stored as the mph
 * all other information is lost when powered down
 * 
 */

void EEPROMWriteInt(int address, int value) {
      //Decomposition from a long to 2 bytes by using bitshift.
      //One = Most significant -> Two = Least significant byte
      byte two = (value & 0xFF);
      byte one = ((value >> 8) & 0xFF);

      //Write the 4 bytes into the eeprom memory.
      EEPROM.write(address, two);
      EEPROM.write(address + 1, one);
}

int EEPROMReadInt(int address) {
      //Read the 2 bytes from the eeprom memory.
      int two = EEPROM.read(address);
      int one = EEPROM.read(address + 1);

      //Return the recomposed long by using bitshift.
      return (((two << 0) & 0xFF) + ((one << 8) & 0xFFFF) & 0xFFFF);
}

/*
 * Function to control button delay and bounce
 */
  
byte delayButton() {
  uint8_t temp = 0;
  if (debounce < millis()) {
    temp = lcd.readButtons();
    if (temp) 
      debounce = millis() + 300;
    //Serial.println(temp);
  }
  
  return temp;
}


/*
* Functions dealing with the submenu system
*/

void printStoredAverage(int row, boolean average[]) {

  int tempFinalTimeInt, tempFinalTimeMod;
  unsigned long averageCombined = 0;
  int averageUnits = 0;
  unsigned long averageFinal;
  
  lcd.setCursor(3, row);
  lcd.print("AVG");
  lcd.setCursor(8, row);

  // average the list based on which ones where selected

  for(int i=0; i < 10; i++) {
    if(average[i]) {
      averageCombined = averageCombined + storedTimeArray[i];
      averageUnits++; 
    }
  }

  if (averageUnits == 0)
    averageFinal = 0;
  else
    averageFinal = averageCombined/averageUnits;
  tempFinalTimeInt = averageFinal/1000;
  tempFinalTimeMod = averageFinal % 1000;

  if (tempFinalTimeInt < 10)
    lcd.print(" ");
  lcd.print(tempFinalTimeInt);
  lcd.print(".");
  if (tempFinalTimeMod < 10)    // if fractional < 10 the 0 is ignored giving a wrong time, so add the zeros
    lcd.print("00");       // add two zeros
  else if (tempFinalTimeMod < 100)
    lcd.print("0");  
  lcd.print(tempFinalTimeMod);  
}
void printStoredTime (int row, int storeSlot, boolean selected) {
  
  int tempFinalTimeInt = storedTimeArray[storeSlot]/1000;
  int tempFinalTimeMod = storedTimeArray[storeSlot] % 1000;
  
  lcd.setCursor(5, row);
  lcd.print(storeSlot + 1);
  lcd.setCursor(8, row);

  if (tempFinalTimeInt < 10)
    lcd.print(" ");
  lcd.print(tempFinalTimeInt);
  lcd.print(".");
  if (tempFinalTimeMod < 10)    // if fractional < 10 the 0 is ignored giving a wrong time, so add the zeros
    lcd.print("00");       // add two zeros
  else if (tempFinalTimeMod < 100)
    lcd.print("0");  
  lcd.print(tempFinalTimeMod);  

  if(selected)
   lcd.print("<");
}

void historyScroll () {
  uint8_t buttonTemp = 0;
  int8_t tempPosition = 0;
  boolean average[10] = {0,0,0,0,0,0,0,0,0,0};
  lcd.clear();
  lcd.setCursor(1,0);

  printStoredTime(0, 0, 0);
  printStoredTime(1, 1, 0);

  while (!(buttonTemp & BUTTON_SELECT)) { //main button scroll loop
    buttonTemp = delayButton();
    
    if (buttonTemp) {
      if (buttonTemp & BUTTON_UP) {
        tempPosition--;
        if (tempPosition == -1)
          tempPosition = 10;
      }
      else if (buttonTemp & BUTTON_DOWN) {
        tempPosition++;
        if (tempPosition == 11)
          tempPosition = 0;
      }
      else if (buttonTemp & BUTTON_RIGHT) {
        average[tempPosition] = ON;
      }
      else if (buttonTemp & BUTTON_LEFT) {
        average[tempPosition] = OFF;
      }

      lcd.clear();

      if(tempPosition == 10)
        printStoredAverage(0, average);
      else 
        printStoredTime(0, tempPosition, average[tempPosition]);
        
      if(tempPosition == 9)
        printStoredAverage(1, average);
      else if (tempPosition == 10)
        printStoredTime(1, 0, average[0]);
      else
        printStoredTime(1,tempPosition + 1, average[tempPosition + 1]); 
    }
  }
}


int numberEnter (int tempNumber) {
  lcd.setCursor(5,1);
  if (tempNumber < 10)
    lcd.print("00");
  else if(tempNumber <100)
    lcd.print("0");
  lcd.print(tempNumber);
  uint8_t buttonTemp = 0;
  uint8_t tempHundred = tempNumber/100;
  uint8_t tempTen = (tempNumber-tempHundred*100)/10;
  uint8_t tempSingle = (tempNumber-tempHundred*100-tempTen*10);
  uint8_t tempPosition = 1;
  
  while (!(buttonTemp & BUTTON_SELECT)) {
     buttonTemp = delayButton();
     if (buttonTemp) {
        if (buttonTemp & BUTTON_UP) {
            if(tempPosition == 1) {
               if(tempHundred == 9)
                  tempHundred = 0;
               else
                    tempHundred++;
               lcd.setCursor(5,1);
               lcd.print(tempHundred);
            }
            else if (tempPosition == 2) {
              if(tempTen == 9)
                tempTen= 0;
              else
                tempTen++;
              lcd.setCursor(6,1);
              lcd.print(tempTen);
            }
            else if (tempPosition == 3) {
              if(tempSingle == 9)
                tempSingle = 0;
              else
                tempSingle++;
              lcd.setCursor(7,1);
              lcd.print(tempSingle);
            }
          }
          else  if (buttonTemp & BUTTON_DOWN) {
            if(tempPosition == 1) {
              if(tempHundred == 0)
                tempHundred = 9;
              else
                tempHundred--;
              lcd.setCursor(5,1);
              lcd.print(tempHundred);
            }
            else if (tempPosition == 2) {
              if(tempTen == 0)
                tempTen = 9;
              else
                tempTen--;
              lcd.setCursor(6,1);
              lcd.print(tempTen);
            }
            else if (tempPosition == 3) {
              if(tempSingle == 0)
                tempSingle = 9;
              else
                tempSingle--;
              lcd.setCursor(7,1);
              lcd.print(tempSingle);
              }
            }
          else if (buttonTemp & BUTTON_LEFT) {
            if (tempPosition == 1) 
              tempPosition = 3;
            else
              tempPosition--;
          }
          else if (buttonTemp & BUTTON_RIGHT) {
            if (tempPosition == 3) 
              tempPosition = 1;
            else
              tempPosition++;
          }
       }   
    }

  return (tempHundred*100+tempTen*10+tempSingle);
}

void setDiameter() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Diameter in MM");

  int tempDiameter = EEPROMReadInt(1);
  int newTempDiameter = tempDiameter;
  tempDiameter = numberEnter(tempDiameter);

  if (newTempDiameter != tempDiameter) {
    diaCalc = tempDiameter*70272;
    timerStart = diaCalc/(timerStartMPH*10000);
    timerStop = diaCalc/(timerStartMPH*10000);
    EEPROMWriteInt(1, tempDiameter);

  }
}

void setStartEnd() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Start Timer MPH");


  int oldStartMPH = numberEnter(timerStartMPH);
  if (oldStartMPH != timerStartMPH) {
    if (oldStartMPH == 0)
      timerStart = 0;
    else
      timerStart = diaCalc/(timerStartMPH*10000);

    EEPROM.write(4, timerStartMPH);
  }


  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Stop Timer MPH");

  int oldStopMPH = numberEnter(timerStopMPH);

  if (oldStopMPH != timerStopMPH) {
   if (timerStopMPH == 0) {
      timerStop = 0;
      //Serial.print("zeroed ");
    }
    else {
      timerStop = diaCalc/(timerStopMPH*10000);
      //Serial.print("not zeroed");
    }

    EEPROM.write(5, timerStopMPH);   
  }
}

void checkTopSpeed() {
  lcd.clear();
  lcd.setCursor (5, 0);
  lcd.print("Max Speed");
  lcd.setCursor(5,1);
  lcd.print(speedMax);
  lcd.print(" mph");

  uint8_t buttonTemp = 0;
  uint8_t positionTemp = 0;
  do {
    buttonTemp = delayButton();

    if ((buttonTemp & BUTTON_UP) || (buttonTemp & BUTTON_DOWN)) {
        if(positionTemp == 0) {
          lcd.setCursor(5,1);
          lcd.print("         ");
          lcd.setCursor(5,1);
          lcd.print("RESET");
          positionTemp = 1;
        }
        else {
          lcd.setCursor(5,1);
          lcd.print("     ");
          lcd.setCursor(5,1);
          lcd.print(speedMax);
          lcd.print(" mph");
          positionTemp = 0;
        }
      }
      else if (buttonTemp & BUTTON_SELECT) {
        if(positionTemp == 1) {
          speedMax = 0;
          EEPROM.write(3, 0);
          lcd.setCursor(5,1);
          lcd.print("CLEAR!");
          delay(1000);
          lcd.setCursor (5, 1);
          lcd.print("Reset ");
        }
        else 
          return;
    }
  } while (1);
}

void mainMenu(uint8_t buttons) {

  const char* menu[5] = {"HISTORY", "TOP SPEED", "SET START/END", "SET DIAMETER", "RETURN"};
  if (menuLevel == 0) {
    menuLevel = 1;
  }
  else {
    if (buttons & BUTTON_UP) {
      if (menuPosition == 0)
        menuPosition = 4;
      else
        menuPosition--;  
    }
    else if (buttons & BUTTON_DOWN) {
      if (menuPosition == 4)
        menuPosition = 0;
      else
        menuPosition++; 
    }
    else if (buttons & BUTTON_SELECT) {
      switch (menuPosition) {
        case 0:
          historyScroll();
          break;
        case 1:
          checkTopSpeed();
          break;
        case 2:
          setStartEnd();
          break;
        case 3:
          setDiameter();
          break;
        case 4:
          menuClear();
          return;
          break;
          
        }
    }
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(">");
  lcd.setCursor(1,0);
  lcd.print(menu[menuPosition]);
  lcd.setCursor(0,1);
  if (menuPosition == 4)
    lcd.print(menu[0]);
  else
    lcd.print(menu[menuPosition + 1]);  
}

/*
* Functions dealing with the main printing menu 
*/

void printMainSpeed(int speedTemp) {
      /* I do not know why but this always double prints the number when it
     *  switches from a single digit to a double digit and writes over the "m"
     *  it does it exacly one time and I don't know why
    lcd.setCursor(9,0);
    if(speedMain < 10)
      lcd.print("  ");
    else if(speedMain < 100)
      lcd.print(" ");
    lcd.print(speedMain);
    */

    /*Looks like a hack but fixes the overwriting issues
    *other versions of the above code also give the double print under the
    *same circumstance, this fixed it
    */
    String strSpeed = String(speedTemp);
    if (speedTemp <10)
      strSpeed = String("  " + strSpeed);
    else if (speedTemp <100)
      strSpeed = String(" " + strSpeed);

    lcd.setCursor(9,0);
    lcd.print(strSpeed);
}

void printTimerCurrent() {
    lcd.setCursor(6,1);
    int tempFinalTimeInt = tempFinalTime/1000;
    if (tempFinalTimeInt < 10)
      lcd.print(" ");
    lcd.print(tempFinalTimeInt);
    lcd.print(".");
    int tempFinalTimeMod = tempFinalTime % 1000;
    if (tempFinalTimeMod < 10)    // if fractional < 10 the 0 is ignored giving a wrong time, so add the zeros
      lcd.print("00");       // add two zeros
    else if (tempFinalTimeMod < 100)
      lcd.print("0");  
    lcd.print(tempFinalTimeMod);  
    //Serial.println(tempFinalTimeMod);
}

void printTimer() {
   if (triggerTimerState == 0) {
      printTimerCurrent();
   }
   else if (triggerTimerState == 1){
      lcd.setCursor(6,1);
      lcd.print("READY ");
    }
    else if (triggerTimerState == 2) {
      tempFinalTime = millis()-triggerTimerElapsed;
      if (tempFinalTime >= 100000) { //timeout at 100 seconds
        tempFinalTime = 100000;
        storedTimeArray[storedTimeWrite] = tempFinalTime;
        storedTimeWrite++;
        if (storedTimeWrite == 10)
          storedTimeWrite = 0;
        triggerTimerState = 0;
      }
      printTimerCurrent();
    }
}


void updateTriggerTimer () {


  // pull infor from the interrups so it can't change during this execution
  noInterrupts();
  unsigned long deltaTemp = triggerTimeDelta;
  byte flagTemp = triggerStartFlag; 
  interrupts();

  if (triggerTimerState == 1) { // state 1 is timer is set to start and ready
    if (timerStart == 0) { // special exception for 0 otherwise it would never start
      if (flagTemp == true) { //special flag to indicate pickup sensor moved
        triggerTimerElapsed = millis();
        triggerTimerState = 2;
      }
    }
    else if (deltaTemp <= timerStart) {
      triggerTimerElapsed = millis();
      triggerTimerState = 2;     
    }
    Serial.print(timerStart);
    Serial.print(" ");
    Serial.println(timerStop);
  }
  else if (triggerTimerState == 2) { //state 2 is running and waiting to see if speed is above a threshold
    if (deltaTemp <= timerStop) {
      triggerTimerState = 0;
      storedTimeArray[storedTimeWrite] = tempFinalTime;
      storedTimeWrite++;
      if (storedTimeWrite == 10)
        storedTimeWrite = 0;

      Serial.println(timerStop);
    }
    Serial.print("waiting for stage 2 ");
    Serial.print(deltaTemp);
    Serial.print(" ");
    Serial.println(timerStop);
    
  }
  
}

void menuClear() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Speed:");
  lcd.setCursor(13, 0);
  lcd.print("mph");
  lcd.setCursor(0,1);
  lcd.print("Time:");
  lcd.setCursor(13, 1);
  lcd.print("sec");
  printMainSpeed(currentSpeed);
  printTimer();
  menuLevel = 0;
}

int updateCurrentSpeed() {

 // calculate speed of bike, set it to change the speed reading only if the number goes up or down
  noInterrupts();
  int speedTemp = diaCalc/(triggerTimeDelta*10000);
  unsigned long timeLastTemp = triggerTimeLast;
  interrupts();
  
  if (speedTemp > speedMax) { // checks for new high speed 
    speedMax = speedTemp;
    EEPROM.write(3, speedTemp);
  }

  if (timeLastTemp+3000 < millis())
    speedTemp = 0;

  return speedTemp;
}


void loop() {
  // set the cursor to column 0, line 1
  // (note: line 1 is the second row, since counting begins with 0):

  if (debounce+2700 < millis()) {
    if(menuLevel)
      menuClear();
      
    printMainSpeed(currentSpeed);
    printTimer();
  }

  currentSpeed = updateCurrentSpeed();
  updateTriggerTimer(); // checks for timer state and begin and end

  uint8_t buttons = delayButton();

  if (buttons) 
    mainMenu(buttons);
}

//interrupt for 
void speedoPickup() {
  triggerTimeDelta = millis() - triggerTimeLast;
  triggerTimeLast = millis(); 
  triggerStartFlag = true;
  //Serial.print("triggered"); 
  //Serial.println(triggerTimeDelta);
}

// Interupt for button to start timer
void timerTriggerButtonInput() {
  //Serial.println("button ok");

  if (timerTriggerButtonInput_Debounce > (millis()))
    return;

  timerTriggerButtonInput_Debounce = millis()+500;
  
  if(triggerTimerState == 0) { 
    triggerTimerState = 1;
    triggerStartFlag = false; //sets up zero starting speed timer exception
    /*
     * keeps false starts from hapening if there is a number stored in 
     * triggerTimeDelta that is less than the beginning delta for 
     * the trigger and it's not a 0 start condition
     */
    triggerTimeDelta = 10000; // just an arbitraty large number to keep from flaggin
  }
  else {
    triggerTimerState = 0;
    triggerStartFlag = true;
  }
}

