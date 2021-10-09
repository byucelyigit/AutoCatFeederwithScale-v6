#include "HX711.h"
#include <CheapStepper.h>
#include <U8g2lib.h>
#include <RtcDS1307.h> //oled
#include <Wire.h> 













// wiring
#define LOADCELL_DOUT_PIN  2
#define LOADCELL_SCK_PIN   3
#define BUTTON4            4 //limitter
#define BUTTON1            5
#define BUTTON2            6
#define BUTTON3            7
#define MOTOR1             8
#define MOTOR2             9
#define STEP1             10
#define STEP2             11
#define STEP3             12
#define STEP4             13

//constants and variables

#define LOADCELL_CHANGETIMEOUT 10000
#define slideDistance            500

#define ModeDoNothing              0
#define ModeMotorRun               1
#define ModeOpenLid                2
#define ModeCloseLid               3
#define ModeTimeout                4 


#define ButtonStatusOpenClose      0
#define ButtonStatusSetTime        1
#define ButtonStatusSetAlarm       2
#define ButtonStatusManuelStart    3

#define calibration_factor       855 //-7050 worked for my 440lb max scale setup

bool moveClockwise               =  true;
bool feedDoorOpen                = false;
long scaleGetUnits               =     0;

int mode = ModeDoNothing; 
int count = 0;
int portionWeightgr = 12;
long oldTime = 0;
long oldScale = 0;
int completedHour = -1;
                  // 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23}
int cmdAmount[24] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,12, 0, 0, 0,12, 0, 0,24};
int cmd[24] =       {0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 1};    //0 do nothing, 1 prepare, 2 open, 3 prepare and open
int nextPointer = 0;
String status = "";
char *statusString[] = {"Ready", "Prep", "Ready", "Ready", "TIMEOUT" };


CheapStepper stepper (STEP1,STEP2,STEP3,STEP4);  
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
RtcDS1307<TwoWire> Rtc(Wire);
HX711 scale;


int CalculateNextPointer(int currentHour) 
{
  int index = (currentHour) % 24;

  for(int i = index; i<24; i++)
  {
    if(cmd[i]>0)
    {
      return i;
    }
  }
}

void resetStepperPins()
{
    digitalWrite(10, LOW);
    digitalWrite(11, LOW);
    digitalWrite(12, LOW);
    digitalWrite(13, LOW);
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printTimeAndAlarm(const RtcDateTime& dt, const RtcDateTime& alrm, String statusStr, long weight, int mode)
{
    char datestring[10];
    char alarmstring[10];
    char weightString[4];
    char modeString[2];
    int x,y;
    x = 12;
    y = 23;

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u:%02u:%02u"),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);

    snprintf_P(alarmstring, 
            countof(alarmstring),
            PSTR("%02uh-%02ugr"),
            alrm.Hour(),
            alrm.Minute());


  int lenStatusString = statusStr.length();
  char statusstring[lenStatusString+1];
    
  statusStr.toCharArray(statusstring, lenStatusString+1);

    snprintf_P(weightString, 
            countof(weightString),
            PSTR("%02ugr"),
            weight);



    snprintf_P(modeString, 
            countof(modeString),
            PSTR("%01u"),
            mode);            


  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_9x15B_mf);
    u8g2.drawStr(x, y, statusstring);
    u8g2.drawStr(x,y+20,datestring);
    u8g2.setFont(u8g2_font_9x15B_mf);
    u8g2.drawStr(x,y+36,alarmstring);
    u8g2.setFont(u8g2_font_9x15_mf);
    u8g2.drawStr(x+60,y,weightString);
    u8g2.setFont(u8g2_font_9x15_mf);
    //u8g2.drawStr(x+85,y,modeString);


    //u8g2.drawFrame(0,0,u8g2.getDisplayWidth(),u8g2.getDisplayHeight() );
  } while ( u8g2.nextPage() );            
}




void setup() {
  Serial.begin(9600);
  Serial.print("setup started.");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();
  Serial.print("scale setup completed.");
  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(MOTOR1, OUTPUT);
  pinMode(MOTOR2, OUTPUT);

  u8g2.begin();
  u8g2.setDisplayRotation(U8G2_R2);

  //Clock related
  //-------------------------------------------------------
  Serial.print("compiled: ");
  Serial.print(__DATE__);
  Serial.println(__TIME__);
  
  Rtc.Begin();

  //RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime compiled = RtcDateTime(__DATE__, "04:59:50");
  Rtc.SetDateTime(compiled);
  //printDateTime(compiled);
  Serial.println();

  //clock related code from lib sample 
  if (!Rtc.IsDateTimeValid()) 
  {
      if (Rtc.LastError() != 0)
      {
          // we have a communications error
          // see https://www.arduino.cc/en/Reference/WireEndTransmission for 
          // what the number means
          Serial.print("RTC communications error = ");
          Serial.println(Rtc.LastError());
      }
      else
      {
          // Common Causes:
          //    1) first time you ran and the device wasn't running yet
          //    2) the battery on the device is low or even missing

          Serial.println("RTC lost confidence in the DateTime!");
          // following line sets the RTC to the date & time this sketch was compiled
          // it will also reset the valid flag internally unless the Rtc device is
          // having an issue

          Rtc.SetDateTime(compiled);
      }
  }

  if (!Rtc.GetIsRunning())
  {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled) 
  {
      Serial.println("RTC is older than compile time!  (Updating DateTime)");
      Rtc.SetDateTime(compiled);
  }
  else if (now > compiled) 
  {
      Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled) 
  {
      Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }
 
  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.SetSquareWavePin(DS1307SquareWaveOut_Low); 
  Serial.print("setup finished. ");
}

void loop() {

  //Clock *********************************************
  if (!Rtc.IsDateTimeValid()) 
  {
      if (Rtc.LastError() != 0)
      {
          // we have a communications error
          // see https://www.arduino.cc/en/Reference/WireEndTransmission for 
          // what the number means
          Serial.print("RTC communications error = ");
          Serial.println(Rtc.LastError());
      }
      else
      {
          // Common Causes:
          //    1) the battery on the device is low or even missing and the power line was disconnected
          Serial.println("RTC lost confidence in the DateTime!");
      }
  }
 
  if (scale.is_ready()) {
    //long reading = scale.read();
    //scaleGetUnits = scale.get_units();
    
    scaleGetUnits = -scale.get_units();
    Serial.print("HX711 reading: ");
    Serial.println(scaleGetUnits,1);
    //Serial.println(reading/1000);
  } else {
    Serial.println("HX711 not found.");
  }
//DrawToOled(2, 30, "test");
//count = count + 1;

  if(digitalRead(BUTTON1) == HIGH)
  {
    Serial.print("button1");
    if(feedDoorOpen == false)
    {
      Serial.print("A1");
      mode = ModeOpenLid;
    }
    else
    {
      Serial.print("B1");
      mode = ModeCloseLid;
    }
  }

  if(digitalRead(BUTTON2) == HIGH)
  {
    Serial.print("button2");
    //scale.set_scale(calibration_factor);
    //scale.tare();
  }

  if(digitalRead(BUTTON3) == HIGH)
  {
    //----------------------------------------------------------
    mode = ModeMotorRun;
    oldTime = millis();
    Serial.print("button3");
  }
  else
  {
    digitalWrite(MOTOR1, LOW);
    digitalWrite(MOTOR2, LOW);

  }

  if(digitalRead(BUTTON4) == HIGH)
  {
    Serial.print("button4");
  }

  if(mode == ModeMotorRun)
  {
    //check for the scale for every 5 sec.
    //if scale is equal to porsion or there is no significant change at scale number then stop the motor
    long myTime = millis();
    digitalWrite(MOTOR1, HIGH);
    digitalWrite(MOTOR2, LOW);

    if(feedDoorOpen)
    {
      stepper.move(!moveClockwise, slideDistance);
      feedDoorOpen = false;   
      resetStepperPins();
      scale.tare();
    }

    if(myTime - oldTime > LOADCELL_CHANGETIMEOUT)
    {
      //if scale is changing then continue
      if(scaleGetUnits > oldScale)
      {
        //new scale measure is larger than previous one.
        oldScale = scaleGetUnits;
      }
      else
      {
        digitalWrite(MOTOR1, LOW);
        digitalWrite(MOTOR2, LOW);
        mode = ModeTimeout;
      }
      oldTime = myTime;
    }

    long wait = 50;
    scale.wait_ready(wait);

    Serial.print(">");
    if(scaleGetUnits >= portionWeightgr)
    {
        //RtcDateTime now = Rtc.GetDateTime();
        //printTimeAndAlarm(now, RtcDateTime(2000,1, 1, alarmHr, alarmMin, 0), status, scaleGetUnits, mode);
        digitalWrite(MOTOR1, LOW);
        digitalWrite(MOTOR2, LOW);
        mode = ModeOpenLid;
    }
    oldScale = scaleGetUnits;
  }


  if(mode == ModeOpenLid)
  {
      stepper.move(moveClockwise, slideDistance);
      feedDoorOpen = true;   
      resetStepperPins();
      mode = ModeCloseLid;
  }

  if(mode == ModeCloseLid)
  {
      stepper.move(!moveClockwise, slideDistance);
      feedDoorOpen = false;   
      mode = ModeDoNothing;
      resetStepperPins();
      scale.tare();
  }

  if(mode == ModeTimeout)
  {
    //if other actions needed. 
    //for example last amount can be writen on oled
    if(!feedDoorOpen)
    {
      stepper.move(moveClockwise, slideDistance);
      feedDoorOpen = true;   
      resetStepperPins();
    }
  }

  RtcDateTime now = Rtc.GetDateTime();
  int next = CalculateNextPointer(now.Hour());//pointer of next command. to calculate hour value +1  of the pointer.;
  printTimeAndAlarm(now, RtcDateTime(2000,1, 1,next+1, cmdAmount[next], 0), statusString[mode], scaleGetUnits, mode);

int hour = now.Hour();
int index = hour - 1;
if(index == -1)
{
  index = 23;
}
if(hour!=completedHour) // runs once
{
  if(cmd[index] = 2)
  {
    mode = ModeOpenLid;
    completedHour = hour;
  }
}

  
  //delay(30);

}

