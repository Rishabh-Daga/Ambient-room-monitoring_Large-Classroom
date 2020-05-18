#include <JSONVar.h>
#include <Arduino_JSON.h>
#include <JSON.h>

#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include <ThingSpeak.h>

// DHT sensor
#define DHTPIN 0     // Digital pin connected to the DHT sensor 
#define DHTTYPE DHT22     // DHT 22

// Pressure sensor
#define BME_SCK 14
#define BME_MISO 12
#define BME_MOSI 13
#define BME_CS 15
#define SEALEVELPRESSURE_HPA (1013.25)
#define ADDR_6713  0x15 // default I2C slave address

// Thingspeak server credentials
unsigned long channel = 905739;
const char* apiKey = "4FL2TX9O4LYX7MLN";

Adafruit_BME280 bme;

DHT dht(DHTPIN, DHTTYPE);

const int sampleWindow = 50; // Sample window width in mS (50 mS = 20Hz)
unsigned int sample;

// ##################### Update the Wifi SSID, Password and IP adress of the server ##########

// WIFI params
char* WIFI_SSID = "esw-m19@iiith"; 
char* WIFI_PSWD = "e5W-eMai@3!20hOct";
//139.59.42.21
String CSE_IP      = "onem2m.iiit.ac.in";
// #######################################################

int WIFI_DELAY  = 100; //ms

// oneM2M : CSE params
int   CSE_HTTP_PORT = 443;
String CSE_NAME    = "in-name";
String CSE_M2M_ORIGIN  = "admin:admin";

// oneM2M : resources' params
String DESC_CNT_NAME = "DESCRIPTOR";
String DATA_CNT_NAME = "DATA";
String CMND_CNT_NAME = "COMMAND";
int TY_AE  = 2;
int TY_CNT = 3;
int TY_CI  = 4;
int TY_SUB = 23;

// HTTP constants
int LOCAL_PORT = 9999;
char* HTTP_CREATED = "HTTP/1.1 201 Created";
char* HTTP_OK    = "HTTP/1.1 200 OK\r\n";
int REQUEST_TIME_OUT = 5000; //ms

//MISC
int SERIAL_SPEED  = 9600;

#define DEBUG

///////////////////////////////////////////

// Global variables
WiFiServer server(LOCAL_PORT);    // HTTP Server (over WiFi). Binded to listen on LOCAL_PORT contant
WiFiClient client;
String context = "";
String command = "";        // The received command



// Method for creating an HTTP POST with preconfigured oneM2M headers
// param : url  --> the url path of the targted oneM2M resource on the remote CSE
// param : ty --> content-type being sent over this POST request (2 for ae, 3 for cnt, etc.)
// param : rep  --> the representaton of the resource in JSON format
String doPOST(String url, int ty, String rep) {

  String postRequest = String() + "POST " + url + " HTTP/1.1\r\n" +
                       "Host: " + CSE_IP + ":" + CSE_HTTP_PORT + "\r\n" +
                       "X-M2M-Origin: " + CSE_M2M_ORIGIN + "\r\n" +
                       "Content-Type: application/json;ty=" + ty + "\r\n" +
                       "Content-Length: " + rep.length() + "\r\n"
                       "Connection: close\r\n\n" +
                       rep;

  // Connect to the CSE address

  Serial.println("connecting to " + CSE_IP + ":" + CSE_HTTP_PORT + " ...");

  // Get a client
  WiFiClient client;
  if (!client.connect(CSE_IP, CSE_HTTP_PORT)) {
    Serial.println("Connection failed !");
    return "error";
  }

  // if connection succeeds, we show the request to be send
#ifdef DEBUG
  Serial.println(postRequest);
#endif

  // Send the HTTP POST request
  client.print(postRequest);

  // Manage a timeout
  unsigned long startTime = millis();
  while (client.available() == 0) {
    if (millis() - startTime > REQUEST_TIME_OUT) {
      Serial.println("Client Timeout");
      client.stop();
      return "error";
    }
  }

  // If success, Read the HTTP response
  String result = "";
  if (client.available()) {
    result = client.readStringUntil('\r');
    //    Serial.println(result);
  }
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  Serial.println();
  Serial.println("closing connection...");
  return result;
}

// Method for creating an ApplicationEntity(AE) resource on the remote CSE (this is done by sending a POST request)
// param : ae --> the AE name (should be unique under the remote CSE)
String createAE(String ae) {
  String aeRepresentation =
    "{\"m2m:ae\": {"
    "\"rn\":\"" + ae + "\","
    "\"api\":\"org.demo." + ae + "\","
    "\"rr\":\"true\","
    "\"poa\":[\"http://" + WiFi.localIP().toString() + ":" + LOCAL_PORT + "/" + ae + "\"]"
    "}}";
#ifdef DEBUG
  Serial.println(aeRepresentation);
#endif
  return doPOST("/" + CSE_NAME, TY_AE, aeRepresentation);
}

// Method for creating an Container(CNT) resource on the remote CSE under a specific AE (this is done by sending a POST request)
// param : ae --> the targeted AE name (should be unique under the remote CSE)
// param : cnt  --> the CNT name to be created under this AE (should be unique under this AE)
String createCNT(String ae, String cnt) {
  String cntRepresentation =
    "{\"m2m:cnt\": {"
    "\"rn\":\"" + cnt + "\","
    "\"min\":\"" + -1 + "\""
    "}}";
  return doPOST("/" + CSE_NAME + "/" + ae, TY_CNT, cntRepresentation);
}

// Method for creating an ContentInstance(CI) resource on the remote CSE under a specific CNT (this is done by sending a POST request)
// param : ae --> the targted AE name (should be unique under the remote CSE)
// param : cnt  --> the targeted CNT name (should be unique under this AE)
// param : ciContent --> the CI content (not the name, we don't give a name for ContentInstances)
String createCI(String ae, String cnt, String ciContent) {
  String ciRepresentation =
    "{\"m2m:cin\": {"
    "\"con\":\"" + ciContent + "\""
    "}}";
  return doPOST("/" + CSE_NAME + "/" + ae + "/" + cnt, TY_CI, ciRepresentation);
}


// Method for creating an Subscription (SUB) resource on the remote CSE (this is done by sending a POST request)
// param : ae --> The AE name under which the SUB will be created .(should be unique under the remote CSE)
//          The SUB resource will be created under the COMMAND container more precisely.
String createSUB(String ae) {
  String subRepresentation =
    "{\"m2m:sub\": {"
    "\"rn\":\"SUB_" + ae + "\","
    "\"nu\":[\"" + CSE_NAME + "/" + ae  + "\"], "
    "\"nct\":1"
    "}}";
  return doPOST("/" + CSE_NAME + "/" + ae + "/" + CMND_CNT_NAME, TY_SUB, subRepresentation);
}


// Method to register a module (i.e. sensor or actuator) on a remote oneM2M CSE
void registerModule(String module, bool isActuator, String intialDescription, String initialData) {
  if (WiFi.status() == WL_CONNECTED) {
    String result;
    // 1. Create the ApplicationEntity (AE) for this sensor
    result = createAE(module);
    if (result == HTTP_CREATED) {
#ifdef DEBUG
      Serial.println("AE " + module + " created  !");
#endif

      // 2. Create a first container (CNT) to store the description(s) of the sensor
      result = createCNT(module, DESC_CNT_NAME);
      if (result == HTTP_CREATED) {
#ifdef DEBUG
        Serial.println("CNT " + module + "/" + DESC_CNT_NAME + " created  !");
#endif


        // Create a first description under this container in the form of a ContentInstance (CI)
        result = createCI(module, DESC_CNT_NAME, intialDescription);
        if (result == HTTP_CREATED) {
#ifdef DEBUG
          Serial.println("CI " + module + "/" + DESC_CNT_NAME + "/{initial_description} created !");
#endif
        }
      }

      // 3. Create a second container (CNT) to store the data  of the sensor
      result = createCNT(module, DATA_CNT_NAME);
      if (result == HTTP_CREATED) {
#ifdef DEBUG
        Serial.println("CNT " + module + "/" + DATA_CNT_NAME + " created !");
#endif

        // Create a first data value under this container in the form of a ContentInstance (CI)
        result = createCI(module, DATA_CNT_NAME, initialData);
        if (result == HTTP_CREATED) {
#ifdef DEBUG
          Serial.println("CI " + module + "/" + DATA_CNT_NAME + "/{initial_aata} created !");
#endif
        }
      }

      // 3. if the module is an actuator, create a third container (CNT) to store the received commands
      if (isActuator) {
        result = createCNT(module, CMND_CNT_NAME);
        if (result == HTTP_CREATED) {
#ifdef DEBUG
          Serial.println("CNT " + module + "/" + CMND_CNT_NAME + " created !");
#endif

          // subscribe to any ne command put in this container
          result = createSUB(module);
          if (result == HTTP_CREATED) {
#ifdef DEBUG
            Serial.println("SUB " + module + "/" + CMND_CNT_NAME + "/SUB_" + module + " created !");
#endif
          }
        }
      }
    }
  }
}


void init_WiFi() {
  Serial.println("Connecting to  " + String(WIFI_SSID) + " ...");
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);

  // wait until the device is connected to the wifi network
  while (WiFi.status() != WL_CONNECTED) {
    delay(WIFI_DELAY);
    Serial.print(".");
  }

  // Connected, show the obtained ip address
  Serial.println("WiFi Connected ==> IP Address = " + WiFi.localIP().toString());
}

void init_pressure() {
  unsigned status;

  // default settings
  // (you can also pass in a Wire library object like &Wire2)
  status = bme.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme.sensorID(), 16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
  }
}

void init_HTTPServer() {
  server.begin();
  Serial.println("Local HTTP Server started !");
}

void task_HTTPServer() {
  // Check if a client is connected
  client = server.available();
  if (!client)
    return;

  // Wait until the client sends some data
  Serial.println("New client connected. Receiving request... ");
  while (!client.available()) {
#ifdef DEBUG_MODE
    Serial.print(".");
#endif
    delay(5);
  }

  // Read the request
  String request = client.readString();
  Serial.println(request);
  client.flush();



  int start, end;
  // identify the right module (sensor or actuator) that received the notification
  // the URL used is ip:port/ae
  start = request.indexOf("/");
  end = request.indexOf("HTTP") - 1;
  context = request.substring(start + 1, end);
#ifdef DEBUG
  Serial.println(String() + start + " , " + end + " -> " + context + ".");
#endif


  // ingore verification messages
  if (request.indexOf("vrq") > 0) {
    client.flush();
    return;
  }


  //Parse the request and identify the requested command from the device
  //Request should be like "[operation_name]"
  start = request.indexOf("[");
  end = request.indexOf("]"); // first occurence of
  command = request.substring(start + 1, end);
#ifdef DEBUG
  Serial.println(String() + start + " , " + end + " -> " + command + ".");
#endif

  client.flush();
}

/////////////////////////////////////////////////////////////////////////////////

// ######################################################## //
// ######### USE THIS SPACE TO DECLARE VARIABLES  ######### //
// ########################################################  //

float temp, hum, soundvolts, pressure, altitude;
//double soundvolts;/

// ########################################################  //

void setup() {
  // intialize the serial liaison
  Serial.begin(SERIAL_SPEED);

  // Connect to WiFi network
  init_WiFi();

  // Start HTTP server
  init_HTTPServer();

  // ######### USE THIS SPACE FOR YOUR SETUP CODE ######### //

  dht.begin();

  Wire.begin();

  init_pressure();

  ThingSpeak.begin(client);

  // ###################################################### //

}

// Main loop of the µController
void loop() {
  // ###############################################################

  // ####################### DHT22 sensor ##########################
  temp = dht.readTemperature();
  hum = dht.readHumidity();

  // ####################### Sound sensor ##########################
  unsigned long startMillis = millis(); // Start of sample window
  unsigned int peakToPeak = 0;   // peak-to-peak level

  unsigned int signalMax = 0;
  unsigned int signalMin = 1024;

  // collect data for 50 mS
  while (millis() - startMillis < sampleWindow)
  {
    sample = analogRead(0);
    if (sample < 1024)  // toss out spurious readings
    {
      if (sample > signalMax)
      {
        signalMax = sample;  // save just the max levels
      }
      else if (sample < signalMin)
      {
        signalMin = sample;  // save just the min levels
      }
    }
  }
  peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude
  soundvolts = (peakToPeak * 5.0) / 1024;  // convert to volts

  // ###################### Pressure sensor #########################

  pressure = bme.readPressure() / 100.0F;
  altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
  
  // ######################################################### //




  // ################################################################### //
  // ######### USE THIS SPACE FOR YOUR SENDING DATA TO SERVER  ######### //
  // ################################################################### //

  /*
      CreateCI(AE_NAME,CONTAINER_NAME,SENSOR_VALUE)

      CreateCI is what posts your sensor data into the container.

      In the below example:
            AE_NAME: Team8_Automated_Driving (As stated in the resource tree)
            CONTAINER_NAME : node_1 ( or as stated in the resource tree)
            SENSOR_VALUE : string of comma separated all sensor values (Eg: 27,25 is temp,hum)
  */

  
  // Storing as a string in a single containers
  String sensor_value_string;
  sensor_value_string = String("DHT : ") + String(temp) + String(",") + String(hum) + String(", Sound : ") + String(soundvolts) + String(", Pressure : ") + String(pressure) + String(",") + String(altitude);
  createCI("Team42_Ambient_classroom-2_Large_class", "node_1", sensor_value_string);
  // Check if the data instance was created.

  // Upload to ThingSpeak
  ThingSpeak.setField(1, temp);
  ThingSpeak.setField(2, hum);
  ThingSpeak.setField(3, soundvolts);
  ThingSpeak.setField(4, pressure);
  ThingSpeak.setField(5, altitude);

  ThingSpeak.writeFields(channel, apiKey);
  
  delay(15000); // DO NOT CHANGE THIS VALUE

 
  // ################################################################### //
}
