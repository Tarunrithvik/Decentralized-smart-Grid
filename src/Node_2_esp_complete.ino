#include <esp_now.h>
#include <WiFi.h>

// MAC Addresses (replace with actual addresses as needed)
uint8_t node1Address[] = {0xF0 , 0x24 , 0xF9 , 0x5A, 0xD0 ,0x74}; // Node 1 (central node)
uint8_t node2Address[] = {0x34, 0x5F, 0x45, 0xA7, 0xE9, 0x24}; // Node 2 (this device, placeholder)
uint8_t node3Address[] = {0xE4, 0x65, 0xB8, 0xE7, 0x5A, 0xEC}; // Node 3
uint8_t node4Address[] = {0x10, 0x06, 0x1C, 0xB5, 0x09, 0x64};// Node 4

// Message type definitions
#define MSG_AUCTION_START 0
#define MSG_BID 1
#define MSG_TRANSACTION_SELL 2
#define MSG_TRANSACTION_BUY 3
#define MSG_DEMAND 4
#define MSG_PRICE 5
#define MSG_COMMAND 6
#define MSG_FORECAST 7
#define MSG_TOKEN 8
#define MSG_VOLTAGE 9

// Unified message structure
typedef struct {
  uint8_t type;
  uint8_t nodeID;
  union {
    struct {
      float demand;
    } demand;
    struct {
      float price;
    } price;
    struct {
      uint8_t command; // 0: Normal, 1: Reduce
    } command;
    struct {
      float forecast;
    } forecast;
    struct {
      float energyKWh;
      float tokenAmount;
      bool transactionRequest;
    } token;
    struct {
      uint8_t action; // 0: sell, 1: buy
      float energy_amount;
      float price_per_unit;
    } bid;
    struct {
      uint8_t buyer_id;
      float energy_amount;
      float price_per_unit;
    } transaction_sell;
    struct {
      uint8_t seller_id;
      float energy_amount;
      float price_per_unit;
    } transaction_buy;
    struct {
      float voltage;
    } voltage;
    struct {
      float value; // Generic float for simple messages (e.g., initial code)
    } simple;
  };
} message;

message myData;
message incomingData;

// Forecasting variables
float previousForecast = 0;
bool firstMeasurement = true;
float alpha = 0.2; // Smoothing factor
unsigned long previousMillis = 0;
const long interval = 10000; // 10s for testing, adjust as needed

// Node-specific variables
const uint8_t nodeID = 2; // This is Node 2
float net_energy = 0.0;   // For energy auction system

// Callback for received data
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataPtr, int len) {
  memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));
  switch (incomingData.type) {
    case MSG_AUCTION_START:
      // Handle auction start (Energy Auction System)
      myData.type = MSG_BID;
      myData.nodeID = nodeID;
      if (net_energy > 0) {
        myData.bid.action = 0; // Sell
        myData.bid.energy_amount = net_energy;
        myData.bid.price_per_unit = 5.0; // Minimum sell price
      } else if (net_energy < 0) {
        myData.bid.action = 1; // Buy
        myData.bid.energy_amount = -net_energy;
        myData.bid.price_per_unit = 6.0; // Maximum buy price
      } else {
        return; // No bid if net energy is zero
      }
      esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
      break;
    case MSG_TRANSACTION_SELL:
      // Handle transaction sell (Energy Auction System)
      Serial.print("Sold ");
      Serial.print(incomingData.transaction_sell.energy_amount);
      Serial.print(" units to Node ");
      Serial.print(incomingData.transaction_sell.buyer_id);
      Serial.print(" at ");
      Serial.print(incomingData.transaction_sell.price_per_unit);
      Serial.println(" INR/unit");
      break;
    case MSG_TRANSACTION_BUY:
      // Handle transaction buy (Energy Auction System)
      Serial.print("Bought ");
      Serial.print(incomingData.transaction_buy.energy_amount);
      Serial.print(" units from Node ");
      Serial.print(incomingData.transaction_buy.seller_id);
      Serial.print(" at ");
      Serial.print(incomingData.transaction_buy.price_per_unit);
      Serial.println(" INR/unit");
      break;
    case MSG_PRICE:
      // Handle price (Dynamic Energy Pricing)
      Serial.print("Received Price: ");
      Serial.print(incomingData.price.price);
      Serial.println(" INR/kWh");
      break;
    case MSG_COMMAND:
      // Handle command (Demand Response)
      Serial.print("C:");
      Serial.println(incomingData.command.command == 0 ? "Normal" : "Reduce");
      Serial.print("Received Command: ");
      Serial.println(incomingData.command.command == 0 ? "Normal" : "Reduce");
      break;
    case MSG_TOKEN:
      // Handle token (Energy Trading & Token System)
      if (incomingData.nodeID == 1) {
        Serial.print("Tokens earned: ");
        Serial.println(incomingData.token.tokenAmount);
      }
      break;
    default:
      break;
  }
}

// Callback for send status
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  // Initialize serial communications
  Serial.begin(115200);              // For debugging and Arduino Uno
  Serial1.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17 for Arduino Uno

  // Set ESP32 as Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Register peers (Node 1 as primary receiver)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, node1Address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Node 1 as peer");
    return;
  }
  // Add other nodes if needed (e.g., Node 3, Node 4)
}

void loop() {
  unsigned long currentMillis = millis();

  // AI-Powered Demand Forecasting
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (Serial1.available()) {
      String data = Serial1.readStringUntil('\n');
      float consumption = data.toFloat();
      float forecast;
      if (firstMeasurement) {
        forecast = consumption;
        firstMeasurement = false;
      } else {
        forecast = alpha * consumption + (1 - alpha) * previousForecast;
      }
      previousForecast = forecast;
      myData.type = MSG_FORECAST;
      myData.nodeID = nodeID;
      myData.forecast.forecast = forecast;
      esp_err_t result = esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
      if (result == ESP_OK) {
        Serial.println("Forecast sent with success");
      } else {
        Serial.println("Error sending forecast");
      }
    }
  }

  // Handle serial input from Arduino Uno
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');

    // Demand Response
    if (line.startsWith("D:")) {
      float demand = line.substring(2).toFloat();
      myData.type = MSG_DEMAND;
      myData.nodeID = nodeID;
      myData.demand.demand = demand;
      esp_err_t result = esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
      if (result == ESP_OK) {
        Serial.print("Sent Demand: ");
        Serial.print(demand);
        Serial.println(" kW");
      } else {
        Serial.println("Error sending demand");
      }
    }

    // Energy Auction System
    else if (line.startsWith("NET_ENERGY:")) {
      net_energy = line.substring(11).toFloat();
    }

    // Voltage Data (Real-Time Data Logging)
    else {
      float voltage = line.toFloat();
      if (voltage > 0) { // Ensure valid data
        myData.type = MSG_VOLTAGE;
        myData.nodeID = nodeID;
        myData.voltage.voltage = voltage;
        esp_err_t result = esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
        if (result == ESP_OK) {
          Serial.println("Voltage sent to Node 1 with success");
        } else {
          Serial.println("Error sending voltage");
        }
      }
    }
  }

  // Energy Trading & Token System
  float energyProduced = Serial.parseFloat(); // Check for energy production data
  if (energyProduced > 0) {
    myData.type = MSG_TOKEN;
    myData.nodeID = nodeID;
    myData.token.energyKWh = energyProduced;
    myData.token.tokenAmount = 0.0;
    myData.token.transactionRequest = true;
    esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
  }

  // Simple ESP-NOW example (for testing compatibility)
  myData.type = MSG_VOLTAGE; // Reusing as a simple message
  myData.nodeID = nodeID;
  myData.simple.value = 2.5;
  esp_now_send(node1Address, (uint8_t *)&myData, sizeof(myData));
  delay(2000); // Maintain original timing
}
