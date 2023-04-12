#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESP32Time.h>
#include <DNSServer.h>
#include <AsyncTCP.h>
#include "UbidotsEsp32Mqtt.h"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include "time.h"



typedef struct{
  unsigned ssidFlag : 1;
  unsigned passwordFlag : 1;
  unsigned scheduleFlag : 1;
  unsigned durationFlag : 1;
  unsigned timestampFlag : 1;
  unsigned isNewWifiConnected : 1;
  unsigned isMqttConnected : 1;
  unsigned manuallyBackup : 1;

}soft_flags;

volatile soft_flags flags;

/*Ubidots Constants*/
const char *UBIDOTS_TOKEN = "BBFF-MUDDPdoNZT2soyNN7ABMCFRLJO6JDH";  // Put here your Ubidots TOKEN
const char *DEVICE_LABEL = "smartdisk";   // Put here your Device label to which data  will be published
const char *VARIABLE_SEND = "backupregister"; // Put here your Variable label to which data  will be published
const char *VARIABLE_RECEIVE = "scheduletime"; // Put here your Variable label to which data  will be suscribed
const char *VARIABLE_MANUALLY_BACKUP = "manuallybackup"; // Put here your Variable label to which data  will be suscribed


Ubidots ubidots(UBIDOTS_TOKEN);

const int diskOutput = 26;         //Disk output
const int led = 2;                 //Led Output
const char* ssidApn = "SmartDisk"; // SSID APN
const char* passwordApn = "12345Smart"; //Password APN
const int faultCounter = 30;

DNSServer dnsServer;       //Define DNS server
AsyncWebServer server(80); //Web Server using port 80

String ssidNew;             //New SSID set by user 
String passwordNew;         //New Password set by user
String scheduleTime = "21:00";       //Schedule Time set by user
int duration = 30;           //Duration set by user
String updateTimestamp;    //Update RTC timestamp
String deviceMac;
char* clientName;

const long currentTimestamp = 1680834212; // Current timestamp
int getHour;
int getMinutes;
int scheduleHour;
int scheduleMinutes;
int scheduleHourPlusDuration;
int scheduleMinutesPlusDuration;
float checkResult;
int counterStart = 0;
int counterEnd = 0;

const char* ntpServer = "co.pool.ntp.org";
const long  gmtOffset_sec = -18000;
const int   daylightOffset_sec = 3600;


ESP32Time rtc;

void spiffsConfiguration(){
  if(!SPIFFS.begin(true)){                     //Begin SPIFFS Connection
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  File file = SPIFFS.open("/index.html");       // Open SPIFFS file
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }

}

void updateRTCwithNtpServer(){
  Serial.println("Updating RTC");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if(getLocalTime(&timeinfo)){
    rtc.setTimeStruct(timeinfo);
  }
  Serial.println(getLocalTime(&timeinfo));
  Serial.println(rtc.getEpoch());
}

void compareTime(){

  getHour = rtc.getHour(true);                               //Get RTC hour
  getMinutes = rtc.getMinute();                              //Get RTC minutes

  scheduleHour =  (scheduleTime.substring(0,2)).toInt();     //Get Schedule Hour
  scheduleMinutes =  (scheduleTime.substring(3,5)).toInt();  //Get Schedule Minute


  float castDuration = duration / 60.00;                        //Convert duration to hours
  int leftHour = (int) castDuration;                           //Get hour to add
  float rightMinute = castDuration - (float)leftHour;                 //Get minute to add

  scheduleHourPlusDuration = scheduleHour + leftHour;        //Add duration hour to schedule hour
  checkResult = rightMinute * 60.00;
  scheduleMinutesPlusDuration = scheduleMinutes + (int) checkResult;  //Add duration minute to schedule minute


  if(getHour == scheduleHour && getMinutes == scheduleMinutes){
    counterStart++;
    if(counterStart == 1){
      Serial.println(scheduleHour);
      Serial.println(scheduleMinutes);
      Serial.println("Current Hour");
      Serial.println(getHour);
      Serial.println("Current Minute");
      Serial.println(getMinutes);
      digitalWrite(diskOutput,LOW);             //Activate Relé
      digitalWrite(led,HIGH);
      Serial.println("BackUp in progress");
      counterEnd = 0;
    }
    
  }                                                                                     
  if(getHour == scheduleHourPlusDuration && getMinutes == scheduleMinutesPlusDuration){ // Check if the date comply
    counterEnd++;
    if(counterEnd == 1){   
      Serial.println(scheduleHourPlusDuration);
      Serial.println(scheduleMinutesPlusDuration);
      Serial.println("Current Hour");
      Serial.println(getHour);
      Serial.println("Current Minute");
      Serial.println(getMinutes);                       
      digitalWrite(diskOutput,HIGH);             //Deactivate Relé
      digitalWrite(led,LOW);
      Serial.println("BackUp has ended");
      counterStart = 0;
    }


  }
  if(flags.isMqttConnected){                  //If the MQTT is connected publish data to Ubidots   

    
    if(getHour == scheduleHour && getMinutes == scheduleMinutes  ){ // If the BackUpt starts
      //updateRTCwithNtpServer();
      
      if(counterStart == 1){
        char scheduleCharInit[scheduleTime.length()+1];
        scheduleTime.toCharArray(scheduleCharInit, scheduleTime.length()+1);
        char init [14] = "Started";
        char contextInit[30];
        ubidots.addContext("BackUp Init", scheduleCharInit);
        ubidots.getContext(contextInit);
        ubidots.add(VARIABLE_SEND, 1, contextInit); // Insert your variable Labels and the value to be sent
        ubidots.publish(DEVICE_LABEL);
      }

    }
    if(getHour == scheduleHourPlusDuration && getMinutes == scheduleMinutesPlusDuration){ //If the BackUp end
     if(counterEnd == 1){
      String scheduleEnd = String(scheduleHourPlusDuration) + ":" + String(scheduleMinutesPlusDuration);
      Serial.println(scheduleEnd);
      char scheduleCharEnd[scheduleEnd.length()+1];
      scheduleEnd.toCharArray(scheduleCharEnd, scheduleTime.length()+1);
      char finish [14] = "Finished";
      char contextFinish[30];
      ubidots.addContext("BackUp Finished", scheduleCharEnd);
      ubidots.getContext(contextFinish);
      ubidots.add(VARIABLE_SEND, 0, contextFinish ); // Insert your variable Labels and the value to be sent
      ubidots.publish(DEVICE_LABEL);
     }

    }
  }

  
}

void apnConfiguration(const char* ssidApn){
  
  Serial.print("Setting AP (Access Point)…");
  WiFi.softAP(ssidApn, passwordApn); //Set access point
  IPAddress AP_LOCAL_IP(192, 168, 1, 1); //Set a fixed IP address
  IPAddress AP_GATEWAY_IP(192, 168, 1, 1);//Set a fixed Gateway address
  IPAddress AP_NETWORK_MASK(255, 255, 255, 0);//Set a fixed Mask address
  WiFi.softAPConfig(AP_LOCAL_IP, AP_GATEWAY_IP, AP_NETWORK_MASK); // Set network configuration
  delay(100);
  Serial.print("AP IP address: ");
  Serial.println(AP_LOCAL_IP);
}

void newWifiConnection( String ssidNewNetwork , String passwordNewPassword, const char* ssidApn){
  char ssidNewNet [ssidNewNetwork.length()+1];
  ssidNewNetwork.toCharArray(ssidNewNet, ssidNewNetwork.length()+1);
  char passNewNet [passwordNewPassword.length()+1];
  passwordNewPassword.toCharArray(passNewNet, passwordNewPassword.length()+1);

  if(strlen(ssidNewNet)!=0){
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidNewNet, passNewNet);          // Connect to the new WiFi network
    for(int i = 0; i < faultCounter ; i ++){                  // Loop to check if the connection fails
      if (WiFi.status() != WL_CONNECTED) {                   // Check if network is connected
        delay(1000);
        Serial.print(".");
        flags.isNewWifiConnected = false;
      }
      else{
        flags.isNewWifiConnected = true;
        Serial.println("");
        Serial.print("WiFi connected to ");
        Serial.println(ssidNewNet);
        Serial.print("Device IP: ");  
        Serial.println(WiFi.localIP()); //Display IP of the device
        deviceMac = WiFi.macAddress();
        clientName = new char[deviceMac.length() + 1];
        break;

      }
    }
  }
  else{
    flags.isNewWifiConnected = false;
  }
  if(!flags.isNewWifiConnected){// If the connection fails, the original SSID is configured
      Serial.println(" ");
      Serial.println("Connection failed");
      apnConfiguration(ssidApn);
  }
}

void callback(char *topic, byte *payload, unsigned int length) //Callback MQTT
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");
  String payloadReceived;

  for (int i = 0; i < length; i++)
  {
    payloadReceived += (char)payload[i];  //Get data from Ubidots variable
    Serial.print((char)payload[i]);
  }

  char topicSchedule[50]; 
  char topicManually[50]; 

  sprintf(topicSchedule, "/v1.6/devices/%s/%s", DEVICE_LABEL, VARIABLE_RECEIVE);
  sprintf(topicManually, "/v2.0/devices/%s/%s/lv", DEVICE_LABEL, VARIABLE_MANUALLY_BACKUP);

  if(String(topic) == topicSchedule ){              // Compare if we receive the schedule topic
    StaticJsonDocument<200> doc;            //Static JSON Document
    DeserializationError error = deserializeJson(doc, payloadReceived); //Deserialize the JSON

    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    String ubidotsKey = doc["context"]["time"]; //Get the key context, time
    int value = doc["value"];                   // Get value (duration)
    duration = value;
    scheduleTime = ubidotsKey;


  }
  else if(String(topic) == topicManually){// Compare if we receive the manually topic

    char contextManually[80];
    if ((char)payload[0] == '1'){      

      ubidots.addContext("Manually", "Start");
      ubidots.getContext(contextManually);
      digitalWrite(diskOutput,LOW);             //Activate Relé
      ubidots.add(VARIABLE_SEND, 1, contextManually ); // Insert your variable Labels and the value to be sent
      ubidots.publish(DEVICE_LABEL);
      Serial.println(rtc.getHour(true));
      Serial.println(rtc.getMinute());
        
    }
    if ((char)payload[0] == '0'){
      
      ubidots.addContext("Manually", "Finish");
      ubidots.getContext(contextManually);
      digitalWrite(diskOutput,HIGH);             //Activate Relé
      ubidots.add(VARIABLE_SEND, 0, contextManually ); // Insert your variable Labels and the value to be sent
      ubidots.publish(DEVICE_LABEL);
        
    }

  }
 
}

void mqttReconnect(){
                                                 // Loop to check if the connection fails
  for(int i = 0; i < faultCounter ; i ++){
    if(ubidots.connect(clientName, UBIDOTS_TOKEN, UBIDOTS_TOKEN)){ // Check if MQTT is connected
      flags.isMqttConnected = true;
      Serial.print("MQTT Connected ");
      break;     
    }        
    else{
      delay(5000);
      Serial.print(".");
      flags.isMqttConnected = false;

    }      
    }
}
 



void mqttConnection(){        //MQTT Connection to Ubidots Broker
  Serial.println("Checking WiFi Connection");  
  newWifiConnection(ssidNew, passwordNew, ssidApn);     
  if(flags.isNewWifiConnected){ // If the Network with internet is connected
    ubidots.setCallback(callback); //Callback MQTT function
    ubidots.setup();
    mqttReconnect();              //Check MQTT Connection
    if(flags.isMqttConnected){    //If MQTT is connected

      char topic[50]; 
      sprintf(topic, "/v1.6/devices/%s/%s", DEVICE_LABEL, VARIABLE_RECEIVE);
      ubidots.subscribe(topic);   //Suscribe to the last dot topic
      Serial.print("Subscribing to");
      Serial.println(topic);
      ubidots.subscribeLastValue(DEVICE_LABEL, VARIABLE_MANUALLY_BACKUP); // Suscribe to Manually BackUp variable

    }
    else{
      apnConfiguration(ssidApn);
    }
  }
  else{
    flags.isMqttConnected = false;
  }

}

class CaptiveRequestHandler : public AsyncWebHandler { //Class that inherits fom the AsyncWebHandler
public:
  CaptiveRequestHandler() {}         //Constructor
  virtual ~CaptiveRequestHandler() {}//Destructor

  bool canHandle(AsyncWebServerRequest *request){ //Returns true when our captive portal is ready to handle any request
    //request->addInterestingHeader("ANY");
    return true;
  }
  void handleRequest(AsyncWebServerRequest *request) {  //Returns the Index when the client is connected to the WiFi
    request->send(SPIFFS,"/index.html", "text/html", false) ;  //Send file stored in the SSPIFFS

  }
};

void setupServer(){ //The server is set to respond to various request
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ 
      request->send(SPIFFS,"/index.html", "text/html", false) ;  //Send file stored in the SSPIFFS
      Serial.println("Client Connected");
  });
    
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) { //Process the data sent in this path and send a request
      String inputMessage;
      String inputParam;
      Serial.println("Receiving data from WebServer");
  
      if (request->hasParam("ssid")) { //Has the query param /WiFi
        inputMessage = request->getParam("ssid")->value();          //Get value of this query param
        inputParam = "ssid";                                        //Input query param
        ssidNew = inputMessage;
        Serial.println(ssidNew);
        flags.ssidFlag = true;
      }

      if (request->hasParam("password")) { //Has the query param /Password
        inputMessage = request->getParam("password")->value();          //Get value of this query param
        inputParam = "password";                                       //Input query param
        passwordNew = inputMessage;
        Serial.println(passwordNew);
        flags.passwordFlag = true;
      }
      if (request->hasParam("scheduleTime")) { //Has the query param /Password
        inputMessage = request->getParam("scheduleTime")->value();          //Get value of this query param
        inputParam = "scheduleTime";                                        //Input query param
        scheduleTime = inputMessage;
        Serial.println(scheduleTime);
        flags.scheduleFlag = true;
      }
      if (request->hasParam("duration")) { //Has the query param /Password
        duration = 0;
        inputMessage = request->getParam("duration")->value();          //Get value of this query param
        inputParam = "duration";                                       //Input query param
        duration = inputMessage.toInt();
        Serial.println(duration);
        flags.durationFlag = true;
      }
      if (request->hasParam("checkdate")) { //Has the query param /Password
        inputMessage = request->getParam("checkdate")->value();          //Get value of this query param
        inputParam = "checkdate"; 
        updateTimestamp = inputMessage;                                      //Input query param
        Serial.println(updateTimestamp);
        flags.timestampFlag = true;
      }
      
      request->send(200, "text/html", "The values entered by you have been successfully sent to the device <br><a href=\"/\">Return to Home Page</a>");
  });
}




void setup() {
  /*Configuration*/
  Serial.begin(115200);
  pinMode(diskOutput, OUTPUT);
  pinMode(led, OUTPUT);
  digitalWrite(led,LOW);
  digitalWrite(diskOutput,HIGH);
  flags.durationFlag = false;
  flags.scheduleFlag = false;
  flags.ssidFlag = false;
  flags.passwordFlag = false;
  flags.timestampFlag = false;
  flags.isNewWifiConnected = false;
  flags.isMqttConnected = false;
  flags.manuallyBackup = false;
  /*Offline date configuration*/
  rtc.setTime(currentTimestamp); //Current timestamp
  /*APN Configuration*/
  apnConfiguration(ssidApn);
  /*SPIFFS Configurarion*/
  spiffsConfiguration();
  /*Sever configuration*/
  setupServer();
  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP()); //Port, Domain (with * take all the domains), IP to be redirected
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
  server.begin();
} 
void loop(){

  if(flags.isMqttConnected){
    if(!ubidots.connected()){
      Serial.println("Disconnected");
      Serial.println(ubidots.connected());
      mqttReconnect();
    }
    ubidots.loop();
  }
  else{
    dnsServer.processNextRequest();
    if(flags.ssidFlag && flags.passwordFlag && flags.scheduleFlag && flags.durationFlag && flags.timestampFlag){


      mqttConnection();                                   //Try to connect to MQTT
   
      char castTimestamp [updateTimestamp.length()+1];     //Update RTC from NTP server
      updateTimestamp.toCharArray(castTimestamp, updateTimestamp.length()+1);
      long updatedTimestamp = atol(castTimestamp);      //Convert char to long
      rtc.setTime(updatedTimestamp - 18000);            //Update RTC time 
      Serial.println("Updating RTC");
      Serial.println(rtc.getEpoch());     
      
      //Reset flags
      flags.ssidFlag = false;
      flags.durationFlag = false;
      flags.timestampFlag = false;
      flags.passwordFlag = false;
      flags.scheduleFlag = false;

    }
  } 
  compareTime();  

 
}