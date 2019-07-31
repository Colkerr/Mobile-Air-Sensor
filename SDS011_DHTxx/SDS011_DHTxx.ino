
//Save sensor readings to flash memory until reboot when broadcast wifi to allow ftp, deletion and date/time input

#define Sensor_User "sensor_user"           //<<<<<<<<<<<<<<<<<<< NOTE change these <<<<<<<<<<
#define Sensor_Password  "sensor_password"  //<<<<<<<<<<<<<<<<<<< and possibly others below <<
#define SerialSpeed 57600
#define DHTTYPE DHT22                  
const long interval = 60 * 1000;        // interval between read sensors (milliseconds)
const long warmup = 30 * 1000;          // warm up seconds for SDS011
int iCycleSecs=60;
int iWarmUpSecs=30;
long waitForSetUp = 5 * 60 * 1000;      // limit wait for setup 
long extendWaitForSetUp = 20 * 60 * 1000;    // after setup started extend to 20 minutes
int rxPin = D1;                         // SDS011 sensor pins on Nodemcu 
int txPin = D2;
int DHTPin = D7;                        // pin for temp/humidity
//------------------above may need to be changed especially Sensor_User and Sensor_Password
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
bool doneSetDateTime = false; 
unsigned long previousMillis = 0;
String fname = "Not_Set_Up_On_Reboot";        //default when power interruption
int iDay; int iMnth; int iYr; int iHr; int iMin; int iSec;
static uint32_t last_minute;

void setup() {
  Serial.begin(57600);         
  sds.begin();
  WiFi.begin();
  WiFi.forceSleepWake();
  Serial.println("wifi started");
  Serial.println(sds.setQueryReportingMode().toString()); // ensures sensor is in 'query' reporting mode

  WiFi.softAP(Sensor_User, Sensor_Password);             // Start the access point
  Serial.print("Access Point started IP adress = ");
  Serial.println(WiFi.softAPIP());               // Show IP address
       
  pinMode(DHTPin, INPUT);
  dht.begin();  
  
  EEPROM.begin(10);
  iCycleSecs = EEPROM.read(0);
  iWarmUpSecs= EEPROM.read(1);
  
  if (SPIFFS.begin()) {   
    Serial.println("SPIFFS opened!");
    saveRecord("YYYY-MM-DD hh:mm,Temp,Humid,PM2.5,PM10");  //heading and indicate if rebooted without date/time input 
    ftpSrv.begin(Sensor_User, Sensor_Password); // port 21
  }

  server.on("/setup", showSetup);
  server.on("/setuptimes", setCycleTimes);
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

//broadcast wifi until doneSetDateTime. Until then handle FTP and server Clients
void loop(void) {
  uint32_t now = millis();
  if ( !doneSetDateTime ) {
      if ( millis() > waitForSetUp ) {
          Serial.println("End of wait time");
          doneSetDateTime = true;  //don't wait forever for setup to start
          delay(100);   //time to write message
          WiFi.mode(WIFI_OFF);
      } else {
        ftpSrv.handleFTP();
        server.handleClient();
      }
  } else {
    if ( millis() - last_minute >= interval) { 
      last_minute += interval;
      maintainClock();
      readSensors();
    }
  }
}

String readSensors() {   //and save to file
      Serial.println("read the sensors");
      sds.wakeup();
      delay(30000); // working 30 seconds
      PmResult pm = sds.queryPm();
      if (pm.isOk()) {
        Serial.print("PM2.5 = ");
        Serial.print(pm.pm25);
        Serial.print(", PM10 = ");
        Serial.println(pm.pm10);
        Serial.println(pm.toString());
      } else {
        Serial.print("Could not read values from sensor, reason: ");
        Serial.println(pm.statusToString());
      }
      Temperature = dht.readTemperature(); 
      Humidity = dht.readHumidity(); 
      //printf("'%08.2f'", 10.3456);  '00010.35'
      Serial.print("temp and humidity  ");Serial.print(Temperature);Serial.print("  ");Serial.println(Humidity);
      String output =  sFormInt(iYr,4,'0') + "-" + sFormInt(iMnth, 2, '0') + "-" + sFormInt(iDay, 2, '0');
      output = output  + " " + sFormInt(iHr, 2, '0') + ":" + sFormInt(iMin, 2, '0');
      output = output + "," + String(Temperature) + "," + String(Humidity) + "," + String(pm.pm25) + "," + String(pm.pm10);
      Serial.println(output);
      saveRecord(output);

      WorkingStateResult state = sds.sleep();
      if (state.isWorking()) {
        Serial.println("Problem with sleeping the sensor.");
      } else {
        Serial.println("Sensor is sleeping");
      }
      return output;
}

void showSetup() {
  waitForSetUp = extendWaitForSetUp; //must be intended restart so allow longer
  server.send(200, "text/html", "<h1>SETUP</h1>"
      "Logging will commence immediately the date is set or the later of "
      "<br> >>> 5 minutes after boot-up."
      "<br> >>> 20 minutes from this screen opening."
      "<br><br>"
      "1) Cycle and warm up times (secs): "
         "<form method='post' action='/setuptimes'/> "
         "&ensp;<input type='number' name='CycleTime' min=15 max=300 size='3' value='" + String(iCycleSecs) + "'/>"
         "&ensp;<input type='number' name='WarmUp' min=5 max=30 size='3' value='" + String(iWarmUpSecs) + "'/>"
         "&ensp;<input type='submit' value='Submit' /> </form>  "     
      "2) Complete File Transfer. <br><br>"
      "3) Delete existing records. <br>"
         "<form  method='post' name='delete' action='/delete_all_records'>"
         "<p><input type='submit' value='Delete All Records' />"
         "</form>"
      "4) Set the Date & Time and show first record - takes 30 seconds.  <br>"
         "<form method='post' name='frm' action='/set_date_time'> "
         "<p><input type='submit' value='Set Date & Time' /> "
         "<input type='text' name='product' size='45' /> "
         "</form> <br> "
         "<script>document.frm.product.value=Date(); </script>"
   );
}

void setCycleTimes() {
  iCycleSecs = server.arg(0).toInt();
  iWarmUpSecs = server.arg(1).toInt();
  Serial.println(String(iCycleSecs) + "  " + String(iWarmUpSecs)) ;
  EEPROM.write(0, iCycleSecs);
  EEPROM.write(1, iWarmUpSecs);
  EEPROM.commit();
  showSetup();
}

void setDateTime() {
  String message = server.arg(0) + "\n";
  Serial.println(message);
  iYr = message.substring(11, 15).toInt();
  iDay = message.substring(8, 10).toInt();
  iHr = message.substring(16, 18).toInt();
  iMin = message.substring(19, 21).toInt();
  iSec = message.substring(22, 24).toInt();
  iMnth = month2Number(message.substring(4, 7));
  last_minute=millis();  //starting point for readings
  message.replace(" ", "");
  fname = message.substring(3, 8) + message.substring(8, 20);
  Serial.println(fname);
  //readSensors in following statement will also save first reacord to file
  server.send(200, "text/html", "Date & Time is " + sFormInt(iYr, 4, '0') + "/" + sFormInt(iMnth, 2, '0')  + "/" + sFormInt(iDay, 2, '0') + 
              " " + sFormInt(iHr, 2, '0') + ":" + sFormInt(iMin, 2, '0') + ":" + sFormInt(iSec, 2, '0') +
              "<br>First record = " + readSensors() + "<br>"
              "<h3>Sensor logging starts now. </h3>" 
              );       
  delay(1000);  
  waitForSetUp = 0;  //start logging
}

void maintainClock() {
  if (++iMin >= 60) {
      iMin = 0;
      if (++iHr >= 24) {
        iHr = 0;
        if (++iDay > daysInMonth()) {
           iDay = 0;
           if (iDay==31 && iMnth==12) {
             ++iYr;
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
  server.send(200, "text/html", "<h2>Data has " + String(deleteStatus ? " " : "NOT " ) + "been deleted.</h2>  <h3><a href='/setup'>Return</a></h3>" );
}

void saveRecord(String output) {
  File sensorData = SPIFFS.open("/" + fname + ".csv", "a");                    
  sensorData.println(output);
  sensorData.close();
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

int daysInMonth() {
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
