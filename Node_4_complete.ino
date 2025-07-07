#include <esp_now.h>
#include <WiFi.h>

// Configurable Node ID (change this based on the ESP32 node: 1, 2, 3, or 4)
const uint8_t node_id = 4;

// Peer MAC Addresses
uint8_t node1Address[] = {0xf0, 0x24, 0xf9, 0x5a, 0xd0, 0x74}; // Node 1
uint8_t centralNodeMac[] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x01}; // Central node for auction
uint8_t broadcastAddress[] = {0xf0, 0x24, 0xf9, 0x5a, 0xd0, 0x74}; // Default receiver

// ---- Message Structures ----

// ESP_NOW Basic Communication
typedef struct {
  float c;
} struct_message_basic;
struct_message_basic basicData;

// AI-Powered Demand Forecasting
typedef struct {
  float forecast;
} struct_message_forecast;
struct_message_forecast forecastData;

// Dynamic Energy Pricing & Demand Response
typedef struct {
  uint8_t type; // 0: demand, 1: price, 2: command
  union {
    struct {
      uint8_t node_id;
      float demand;
    } demand;
    struct {
      float price;
    } price;
    struct {
      uint8_t command;
    } command;
  };
} message_pricing;
message_pricing pricingData;

// Energy Auction System
#define MSG_AUCTION_START 0
#define MSG_BID 1
#define MSG_TRANSACTION_SELL 2
#define MSG_TRANSACTION_BUY 3

typedef struct {
  uint8_t type; // MSG_AUCTION_START
} auction_start_msg;

typedef struct {
  uint8_t type; // MSG_BID
  uint8_t node_id;
  uint8_t action; // 0 for sell, 1 for buy
  float energy_amount;
  float price_per_unit;
} bid_msg;
bid_msg bidData;

typedef struct {
  uint8_t type; // MSG_TRANSACTION_SELL
  uint8_t buyer_id;
  float energy_amount;
  float price_per_unit;
} transaction_sell_msg;

typedef struct {
  uint8_t type; // MSG_TRANSACTION_BUY
  uint8_t seller_id;
  float energy_amount;
  float price_per_unit;
} transaction_buy_msg;

// Energy Trading & Token System
typedef struct {
  uint8_t nodeID;
  float energyKWh;
  float tokenAmount;
  bool transactionRequest;
} struct_message_trading;
struct_message_trading tradingData;

// Real-Time Data Logging and Analytics / ESP32 Web Dashboard
typedef struct {
  int nodeID;
  float voltage;
} struct_message_logging;
struct_message_logging loggingData;

// ---- Global Variables ----

// Peer info structure
esp_now_peer_info_t peerInfo;

// Forecasting variables
float previousForecast = 0;
bool firstMeasurement = true;
float alpha = 0.2;
const int sensorPin = 34; // ADC pin for forecasting

// Timing variables
unsigned long previousMillisForecast = 0;
unsigned long previousMillisDemand = 0;
unsigned long previousMillisLogging = 0;
const long intervalForecast = 10000; // 10s
const long intervalDemand = 5000;   // 5s
const long intervalLogging = 2000;  // 2s

// ---- Callback Functions ----

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len < 1) return;

  // Energy Trading & Token System
  if (len == sizeof(struct_message_trading)) {
    memcpy(&tradingData, incomingData, sizeof(tradingData));
    Serial.print("Node ");
    Serial.print(tradingData.nodeID);
    Serial.print(": Energy = ");
    Serial.print(tradingData.energyKWh);
    Serial.print(" kWh, Tokens = ");
    Serial.println(tradingData.tokenAmount);
    return;
  }

  // Dynamic Energy Pricing & Demand Response
  if (len == sizeof(message_pricing)) {
    memcpy(&pricingData, incomingData, sizeof(pricingData));
    if (pricingData.type == 1) { // Price message
      Serial.print("Received Price: ");
      Serial.print(pricingData.price.price);
      Serial.println(" INR/kWh");
    }
    return;
  }

  // Energy Auction System
  uint8_t type = incomingData[0];
  if (type == MSG_AUCTION_START) {
    float net_energy = (random(-100, 100)) / 10.0; // Simulated net energy
    bidData.type = MSG_BID;
    bidData.node_id = node_id;
    if (net_energy > 0) {
      bidData.action = 0; // Sell
      bidData.energy_amount = net_energy;
      bidData.price_per_unit = 5.0;
    } else if (net_energy < 0) {
      bidData.action = 1; // Buy
      bidData.energy_amount = -net_energy;
      bidData.price_per_unit = 6.0;
    } else {
      return; // No bid if net energy is zero
    }
    esp_now_send(centralNodeMac, (uint8_t*)&bidData, sizeof(bidData));
  } else if (type == MSG_TRANSACTION_SELL && len == sizeof(transaction_sell_msg)) {
    transaction_sell_msg ts;
    memcpy(&ts, incomingData, sizeof(ts));
    Serial.print("Sold ");
    Serial.print(ts.energy_amount);
    Serial.print(" units to Node ");
    Serial.print(ts.buyer_id);
    Serial.print(" at ");
    Serial.print(ts.price_per_unit);
    Serial.println(" INR/unit");
  } else if (type == MSG_TRANSACTION_BUY && len == sizeof(transaction_buy_msg)) {
    transaction_buy_msg tb;
    memcpy(&tb, incomingData, sizeof(tb));
    Serial.print("Bought ");
    Serial.print(tb.energy_amount);
    Serial.print(" units from Node ");
    Serial.print(tb.seller_id);
    Serial.print(" at ");
    Serial.print(tb.price_per_unit);
    Serial.println(" INR/unit");
  }
}

// ---- Setup Function ----

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register Node 1 as peer
  memcpy(peerInfo.peer_addr, node1Address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Node 1 peer");
    return;
  }

  // Register central node as peer (for auction)
  memcpy(peerInfo.peer_addr, centralNodeMac, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add central node peer");
    return;
  }

  // Initialize data
  loggingData.nodeID = node_id;
  basicData.c = 2.5;
}

// ---- Loop Function ----

void loop() {
  unsigned long currentMillis = millis();

  // ESP_NOW Basic Communication (every 2s)
  if (currentMillis - previousMillisLogging >= intervalLogging) {
    previousMillisLogging = currentMillis;
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&basicData, sizeof(basicData));
    if (result == ESP_OK) Serial.println("Basic data sent with success");
    else Serial.println("Error sending basic data");
  }

  // AI-Powered Demand Forecasting (every 10s)
  if (currentMillis - previousMillisForecast >= intervalForecast) {
    previousMillisForecast = currentMillis;
    int sensorValue = analogRead(sensorPin);
    float voltage = sensorValue * (3.3 / 4095.0);
    float consumption = voltage * 10.0;
    float forecast;
    if (firstMeasurement) {
      forecast = consumption;
      firstMeasurement = false;
    } else {
      forecast = alpha * consumption + (1 - alpha) * previousForecast;
    }
    previousForecast = forecast;
    forecastData.forecast = forecast;
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&forecastData, sizeof(forecastData));
    if (result == ESP_OK) Serial.println("Forecast sent with success");
    else Serial.println("Error sending forecast");
  }

  // Dynamic Energy Pricing & Demand Response (every 5s)
  if (currentMillis - previousMillisDemand >= intervalDemand) {
    previousMillisDemand = currentMillis;
    float demand = 4.0 + sin(currentMillis / 10000.0) * 1.0;
    pricingData.type = 0; // Demand message
    pricingData.demand.node_id = node_id;
    pricingData.demand.demand = demand;
    esp_err_t result = esp_now_send(node1Address, (uint8_t*)&pricingData, sizeof(pricingData));
    if (result == ESP_OK) {
      Serial.print("Sent Demand: ");
      Serial.print(demand);
      Serial.println(" kW");
    } else {
      Serial.println("Error sending demand");
    }
  }

  // Real-Time Data Logging and Analytics (every 2s)
  if (currentMillis - previousMillisLogging >= intervalLogging) {
    loggingData.voltage = analogRead(34) * (3.3 / 4095.0);
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*)&loggingData, sizeof(loggingData));
    if (result == ESP_OK) Serial.println("Logging data sent with success");
    else Serial.println("Error sending logging data");
  }

  // Energy Auction System and Trading handled in OnDataRecv callback
}