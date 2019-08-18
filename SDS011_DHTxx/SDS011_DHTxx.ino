

//Save sensor readings to flash memory until reboot when broadcast wifi to allow ftp, deletion and date/time input
//Using Deep Sleep so wakes evey time into setup() and any only persistence is by using RTC memory

#define Sensor_User "sensor_user"           //<<<<<<<<<<<<<<<<<<< NOTE change these and possibly others below <<<<<<<<<<
#define Sensor_Password  "password"         //>>>> WARNING password may have to be at least 8 characters or WiFI.softAP(..) fails.
#define DHTTYPE DHT11                  
int iCycleSecs=60;                          //value input on set up and stored in EEPROM
int iWarmUpSecs=30;                         //                  ditto
long waitForSetUp = 2 * 60 * 1000;          // time out wait for setup 
long extendWaitForSetUp = 20 * 60 * 1000;   // once setup started extend to 20 minutes
int rxPin = D5;                             // SDS011 sensor pins on Nodemcu 
int txPin = D6;
int DHTPin = D7;                            // pin for temp/humidity
//------------------above may need to be changed especially Sensor_User and Sensor_Password

extern "C" {          // this is for the RTC memory read/write functions
  #include "user_interface.h" 
}

typedef struct {      // this is for the RTC memory read/write functions
  char cycleState;    //?=begin, 1=warm up, 2=rest (cycle-warmup)
  byte fixTime;       //units of 0.1% 
  char spare[3];      //make up to 4 bytes
  int iYr;            //4 bytes
  int iMnth;    
  int iDay;      
  int iHr;      
  int iMin;       
  int iSec;
  int timings[2];     //Cycle time and warm up time in seconds 
} rtcStore;

rtcStore rtcMem;
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>   // Include the SPIFFS library
#include <EEPROM.h>
#include "ESP8266FtpServer.h"
#include "SdsDustSensor.h"
#include "DHT.h"

DHT dht(DHTPin, DHTTYPE);

SdsDustSensor sds(rxPin, txPin);

int Temperature;
int Humidity;
 
ESP8266WebServer server(80);    // Create a webserver object that listens for HTTP request on port 80
FtpServer ftpSrv;

bool handleFileRead(String path);       // send the right file to the client (if it exists)
unsigned long previousMillis = 0;
String fname = "SensorFile";       

void setup() {
  Serial.begin(57600);         
  EEPROM.begin(3);
  
  system_rtc_mem_read(64, &rtcMem, sizeof(rtcMem));       //all variables lost after sleep, restore from RTC memory
  byte checkcyclestate=rtcMem.cycleState;

  if (rtcMem.cycleState=='1') {    //>>>>>>>>>>>>>Start WARM UP then sleep<<<<<<<<<<<<<
    Serial.println("state 1, Warm up ");
    sds.wakeup();
    rtcMem.cycleState='2';
    system_rtc_mem_write(64, &rtcMem, sizeof(rtcMem));
    Serial.println("SLEEP state 1");
    delay(100);
    ESP.deepSleep(rtcMem.timings[1] * 1e6, WAKE_RF_DISABLED);   //sleep for warm up seconds
  } else if (rtcMem.cycleState=='2') {        //>>>>>>>>>>>>>READ SENSORS THEN SLEEP<<<<<<<<<<<<<
    pinMode(DHTPin, INPUT); 
    sds.begin();
    Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode
    Serial.println("state 2, read sensors ");
    rtcMem.cycleState='1';                         //next state
    readSensors();   //and save data to SPIFFS
    long adjustedCycle= ( 1e6 + 1000*(rtcMem.fixTime-100) ) * rtcMem.timings[0];  //fixTime in 0.1% units
    maintainClock(adjustedCycle);
    system_rtc_mem_write(64, &rtcMem, sizeof(rtcMem));
    Serial.println(" Sleep until state 1");
    ESP.deepSleep( adjustedCycle - ( rtcMem.timings[1] ) * 1e6, WAKE_RF_DISABLED);   //sleep remainder of cycle
  } else { 
    delay(3000);
    Serial.println("Initialise start up -> setup ");
    rtcMem.timings[0]=EEPROM.read(0);
    rtcMem.timings[1]=EEPROM.read(1);
    delay(100);     
    WiFi.begin();
    WiFi.forceSleepWake();
    Serial.println("wifi started");
    if (!WiFi.softAP(Sensor_User, Sensor_Password)) {Serial.println("false returned from WiFi.softAP() "); }             // Start the access point
    Serial.print("Access Point started IP adress = ");
    Serial.println(WiFi.softAPIP());               // Show IP address

    sds.begin();

    if (SPIFFS.begin()) {   
      Serial.println("SPIFFS opened!");
      saveRecord("YYYY-MM-DD hh:mm,Temp,Humid,PM2.5,PM10");  //heading and mark file if rebooted without set up
      ftpSrv.begin(Sensor_User, Sensor_Password); // port 21
    }
    
    server.on("/setup", showSetup);
    server.on("/setuptimes", setCycleTimes);
    server.on("/setupclockadjust", setClockAdjust);
    server.on("/set_date_time", setDateTime);
    server.on("/delete_all_records", deleteAllRecords);
    server.onNotFound([]() {                              // If the client requests any URI
      if (!handleFileRead(server.uri()))                  // send it if it exists
        server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
    });
  
    server.begin();                           
    Serial.println("HTTP server started");
    delay(10);
  } 
}

//only here when power off reboot until set up completed or times out and reverts to logging
//broadcast wifi, handle FTP and server Clients
void loop(void) {
    if ( millis() > waitForSetUp ) {
        Serial.println("End of wait time");            //start cycling
        rtcMem.cycleState='1';                         //next state
        readSensors();   //and save data to SPIFFS
        long adjustedCycle= ( 1e6 + 1000*(rtcMem.fixTime-100) ) * rtcMem.timings[0];  //fixTime in 0.1% units
        rtcMem.iYr=0;rtcMem.iMnth=0;rtcMem.iDay=0;rtcMem.iHr=0;rtcMem.iMin=0;rtcMem.iSec=0;  //default when not set explicitly
        system_rtc_mem_write(64, &rtcMem, sizeof(rtcMem));
        WiFi.mode(WIFI_OFF);
        Serial.println(" Sleep until state 1");
        delay(10);
        ESP.deepSleep( adjustedCycle - ( rtcMem.timings[1] ) * 1e6, WAKE_RF_DISABLED);   //sleep remainder of cycle
    } else {
      ftpSrv.handleFTP();
      server.handleClient();
    }
    delay(10);
}

String readSensors() {   //and save to file
      dht.begin();  
      Serial.println("read the sensors");
      PmResult pm = sds.queryPm();
      if (pm.isOk()) {
        Serial.print("PM2.5 = " + String(pm.pm25) );
        Serial.print(", PM10 = " + String(pm.pm10) );
      } else {
        Serial.println("Sensor problem, reason: " + pm.statusToString());
      }
      Temperature = dht.readTemperature(); 
      Humidity = dht.readHumidity(); 
      String output =  sFormInt(rtcMem.iYr,4,'0') + "-" + sFormInt(rtcMem.iMnth, 2, '0') + "-" + sFormInt(rtcMem.iDay, 2, '0');
      output = output  + " " + sFormInt(rtcMem.iHr, 2, '0') + ":" + sFormInt(rtcMem.iMin, 2, '0')  + ":" + sFormInt(rtcMem.iSec, 2, '0');
      output = output + "," + String(Temperature) + "," + String(Humidity) + "," + String(pm.pm25) + "," + String(pm.pm10);
      sds.sleep();
      Serial.println(output);
      saveRecord(output);
      return(output);     // for html if called from setDateTime()
}

void showSetup() {
  waitForSetUp = extendWaitForSetUp; //must be intended restart so allow longer
  
  server.send(200, "text/html", "<h1>SETUP</h1>"
      "Logging will commence immediately the date is set or the later of "
      "<br> >>> 2 minutes after boot-up."
      "<br> >>> 20 minutes from this screen opening."
      "<br> >>> Only submit 1) & 2) if changing values."
      "<br><br>"
      "1) Cycle and warm up times (secs): "
         "<form method='post' action='/setuptimes'/> "
         "&ensp;<input type='number' name='CycleTime' min='15' max='255' size='3' value='" + String(EEPROM.read(0)) + "'/>"
         "&ensp;<input type='number' name='WarmUp' min='5' max='30' size='3' value='" + String( EEPROM.read(1)) + "'/>"
         "&ensp;<input type='submit' value='Submit' /> </form>  "
      "2) Adjust clock in ( units of 0.1% ): "
          "<form method='post' action='/setupclockadjust'/> "
           "&ensp;<input type='number' name='ClockAdjust' min= '-100' max= '100' size='3' value='" + String( EEPROM.read(2) - 100 ) + "'/>"
           "&ensp;<input type='submit' value='Submit' /> </form>  "
      "3) Complete File Transfer. <br><br>"
      "4) Delete existing records. <br>"
         "<form  method='post' name='delete' action='/delete_all_records'>"
         "<p><input type='submit' value='Delete All Records' />"
         "</form>"
      "5) Set the Date & Time and show first record.  <br>"
         "<form method='post' name='frm' action='/set_date_time'> "
         "<p><input type='submit' value='Set Date & Time' /> "
         "<input type='text' name='product' size='45' /> "
         "</form> <br> "
         "<script>document.frm.product.value=Date(); </script>"
   );
}

void setCycleTimes() {
  rtcMem.timings[0] = server.arg(0).toInt();  //complete cycle in seconds
  rtcMem.timings[1] = server.arg(1).toInt();  //warmup in seconds
  Serial.println(String(iCycleSecs) + "  " + String(iWarmUpSecs)) ;
  if ( EEPROM.read(0)!=rtcMem.timings[0] ) EEPROM.write(0, rtcMem.timings[0]); //store for reboot on power outage
  if ( EEPROM.read(1)!=rtcMem.timings[1] ) EEPROM.write(1, rtcMem.timings[1]); 
  EEPROM.commit();
  showSetup();
}

void setClockAdjust() {
   rtcMem.fixTime= server.arg(0).toInt() + 100 ;
   Serial.print("fix time " );Serial.println(rtcMem.fixTime);
   EEPROM.write(2, rtcMem.fixTime);
   Serial.println(EEPROM.read(2));
   EEPROM.commit();
   showSetup();
}

void setDateTime() {
  String message = server.arg(0) + "\n";
  Serial.println(message);
  rtcMem.iMnth = month2Number(message.substring(4, 7));
  rtcMem.iYr = message.substring(11, 15).toInt();
  rtcMem.iDay = message.substring(8, 10).toInt();
  rtcMem.iHr = message.substring(16, 18).toInt();
  rtcMem.iMin = message.substring(19, 21).toInt();
  rtcMem.iSec = message.substring(22, 24).toInt();

  //readSensors in following statement will also save first reacord to file
  server.send(200, "text/html", "Date & Time is " + sFormInt(rtcMem.iYr, 4, '0') + "/" + sFormInt(rtcMem.iMnth, 2, '0')  + "/" + sFormInt(rtcMem.iDay, 2, '0') + 
              " " + sFormInt(rtcMem.iHr, 2, '0') + ":" + sFormInt(rtcMem.iMin, 2, '0') + ":" + sFormInt(rtcMem.iSec, 2, '0') +
              "<br>First record = " + readSensors() + "<br>"
              "<h3>Sensor logging starts now. </h3>" 
              );       
  delay(100);  
  rtcMem.cycleState='1';    
  system_rtc_mem_write(64, &rtcMem, sizeof(rtcMem));
  Serial.print("SLEEP after set date time ");
  Serial.println(rtcMem.timings[1]);
  delay(100);
  ESP.deepSleep(rtcMem.timings[1] * 1e6, WAKE_RF_DISABLED);   //sleep for remainder of cycle
}

void maintainClock(long adjustedCycle) {
  rtcMem.iSec += adjustedCycle;
  if (rtcMem.iSec >=60) {  
    rtcMem.iSec=rtcMem.iSec % 60;
    if (++rtcMem.iMin >= 60) {
        rtcMem.iMin = 0;
        if (++rtcMem.iHr >= 24) {
          rtcMem.iHr = 0;
          if (++rtcMem.iDay > daysInMonth(rtcMem.iYr,rtcMem.iDay,rtcMem.iMnth)) {
             rtcMem.iDay = 0;
             if (rtcMem.iDay==31 && rtcMem.iMnth==12) {
               ++rtcMem.iYr;
             }
          }
       }
    }
  }
}

void deleteAllRecords() {              // When URI / is requested, send a web page with a button to toggle the LED
  bool deleteStatus = true;
  Serial.println("handle delete");
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String fname = dir.fileName();
    Serial.print(fname + "  ");
    File f = dir.openFile("r");
    Serial.println(f.size());
    f.close();
    deleteStatus = deleteStatus && SPIFFS.remove(fname);
  }
  saveRecord("YYYY-MM-DD hh:mm,Temp,Humid,PM2.5,PM10");  //heading 
  server.send(200, "text/html", "<h2>Data has " + String(deleteStatus ? " " : "NOT " ) + "been deleted.</h2>  <h3><a href='/setup'>Return</a></h3>" );
}

void saveRecord(String output) {
  if (SPIFFS.begin() ) {
    File sensorData = SPIFFS.open("/" + fname + ".csv", "a");    
    if (!sensorData) {
      Serial.println("file open failed");
    } else {
        sensorData.println(output);
        delay(100);
        Serial.println("written to file ");
        sensorData.close();
    }
  } else {
    Serial.println("SPIFFS begin failed");
  }       
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  // if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  // String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    Serial.println("file exists");
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, "text/plain"); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

int month2Number(String sMonth) {
  String sMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  int i = 0;
  while (sMonths[i] != sMonth) {
    i += 1;
  }
  return i + 1;
}

int daysInMonth(int iYr,int iMnth, int iDay) {
  int daysInMonths[12] = {31, 28, 31, 30, 31, 30, 31, 30, 31, 31, 30, 31};
  int numDays = daysInMonths[iMnth - 1];
  if (iMnth = 2)  {
    if (iYr % 4  == 0) {
      if (iYr % 100 != 0) {
        numDays = 29;
      } else {
        if (iYr % 400 == 0) {
          numDays = 29;
        }
      }
    }
  }
  return numDays;
}

//////////////////////Format integer
String sFormInt(int n, int i, char sP) {  //n to be formatted as i digits, leading sP
  String sN = String(n);
  int j = i - sN.length();
  for (int k = 0; k < j; k++)  {
    sN = sP + sN;
  }
  return sN;
}
