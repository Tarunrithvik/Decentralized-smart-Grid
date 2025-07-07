#include <esp_now.h>
#include <WiFi.h>

// MAC Addresses
uint8_t node1Address[] = {0xf0, 0x24, 0xf9, 0x5a, 0xd0, 0x74}; // Node 1 (receiver/central node)
uint8_t centralNodeMac[] = {0xf0, 0x24, 0xf9, 0x5a, 0xd0, 0x74}; // Example central node for auction

// Node identifier
const uint8_t nodeID = 3;

// Message type definitions for Energy Auction
#define MSG_AUCTION_START 0
#define MSG_BID 1
#define MSG_TRANSACTION_SELL 2
#define MSG_TRANSACTION_BUY 3

// Data Structures
// Basic ESP-NOW message (first document)
typedef struct struct_message_basic {
  float c;
} struct_message_basic;

// Forecasting message (second document)
typedef struct struct_message_forecast {
  float forecast;
} struct_message_forecast;

// Demand/Price/Command message (third document)
typedef struct message_demand {
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
} message_demand;

// Auction messages (fourth document)
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

// Token system message (fifth document)
typedef struct struct_message_token {
  uint8_t nodeID;
  float energyKWh;
  float tokenAmount;
  bool transactionRequest;
} struct_message_token;

// Voltage logging message (sixth document)
typedef struct struct_message_voltage {
  int nodeID;
  float voltage;
} struct_message_voltage;

// Global variables
struct_message_basic basicData;
struct_message_forecast forecastData;
message_demand demandData;
struct_message_token tokenData;
struct_message_voltage voltageData;

// Incoming data for token system
struct_message_token incomingTokenData;

// Forecasting variables
float previousForecast = 0;
bool firstMeasurement = true;
float alpha = 0.2; // Smoothing factor
float net_energy = 0.0; // For auction system

// Timing variables
unsigned long previousForecastMillis = 0;
const long forecastInterval = 10000; // 10s for testing
unsigned long previousTokenMillis = 0;
const long tokenInterval = 10000; // 10s interval
unsigned long previousBasicMillis = 0;
const long basicInterval = 2000; // 2s interval

// Peer info
esp_now_peer_info_t peerInfo;

// Callback Functions
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  // Demand/Price/Command (third document)
  message_demand msg;
  memcpy(&msg, incomingData, sizeof(msg));
  if (msg.type == 1) { // Price message
    Serial.print("Received Price: ");
    Serial.print(msg.price.price);
    Serial.println(" INR/kWh");
  } else if (msg.type == 2) { // Command message
    Serial.print("Received Command: ");
    Serial.println(msg.command.command == 0 ? "Normal" : "Reduce");
  }

  // Auction system (fourth document)
  if (len >= 1) {
    uint8_t type = incomingData[0];
    if (type == MSG_AUCTION_START) {
      bid_msg bid;
      bid.type = MSG_BID;
      bid.node_id = nodeID;
      if (net_energy > 0) {
        bid.action = 0; // Sell
        bid.energy_amount = net_energy;
        bid.price_per_unit = 5.0; // Minimum sell price
      } else if (net_energy < 0) {
        bid.action = 1; // Buy
        bid.energy_amount = -net_energy;
        bid.price_per_unit = 6.0; // Maximum buy price
      } else {
        return; // No bid if net energy is zero
      }
      esp_now_send(centralNodeMac, (uint8_t*)&bid, sizeof(bid));
    } else if (type == MSG_TRANSACTION_SELL && len == sizeof(transaction_sell_msg)) {
      transaction_sell_msg* ts = (transaction_sell_msg*)incomingData;
      Serial.print("Sold ");
      Serial.print(ts->energy_amount);
      Serial.print(" units to Node ");
      Serial.print(ts->buyer_id);
      Serial.print(" at ");
      Serial.print(ts->price_per_unit);
      Serial.println(" INR/unit");
    } else if (type == MSG_TRANSACTION_BUY && len == sizeof(transaction_buy_msg)) {
      transaction_buy_msg* tb = (transaction_buy_msg*)incomingData;
      Serial.print("Bought ");
      Serial.print(tb->energy_amount);
      Serial.print(" units from Node ");
      Serial.print(tb->seller_id);
      Serial.print(" at ");
      Serial.print(tb->price_per_unit);
      Serial.println(" INR/unit");
    }
  }

  // Token system (fifth document)
  memcpy(&incomingTokenData, incomingData, sizeof(incomingTokenData));
  if (incomingTokenData.nodeID == 1) {
    Serial.print("Tokens spent: ");
    Serial.println(incomingTokenData.tokenAmount);
  }
}

void setup() {
  // Initialize Serial ports
  Serial.begin(115200); // For debugging and Uno communication
  Serial1.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17 for Uno (forecasting)

  // Set WiFi mode
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

  // Add Node 1 as peer
  memcpy(peerInfo.peer_addr, node1Address, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Node 1 as peer");
    return;
  }

  // Add central node for auction (if different from Node 1)
  if (memcmp(centralNodeMac, node1Address, 6) != 0) {
    memcpy(peerInfo.peer_addr, centralNodeMac, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add central node as peer");
      return;
    }
  }

  // Initialize node-specific data
  voltageData.nodeID = nodeID;
  tokenData.nodeID = nodeID;
  tokenData.tokenAmount = 0.0;
  tokenData.transactionRequest = false;
}

void loop() {
  unsigned long currentMillis = millis();

  // Basic ESP-NOW transmission (first document)
  if (currentMillis - previousBasicMillis >= basicInterval) {
    previousBasicMillis = currentMillis;
    basicData.c = 2.5;
    esp_err_t result = esp_now_send(node1Address, (uint8_t*)&basicData, sizeof(basicData));
    if (result == ESP_OK) {
      Serial.println("Basic data sent with success");
    } else {
      Serial.println("Error sending basic data");
    }
  }

  // AI-Powered Demand Forecasting (second document)
  if (currentMillis - previousForecastMillis >= forecastInterval) {
    previousForecastMillis = currentMillis;
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
      forecastData.forecast = forecast;
      esp_err_t result = esp_now_send(node1Address, (uint8_t*)&forecastData, sizeof(forecastData));
      if (result == ESP_OK) {
        Serial.println("Forecast sent with success");
      } else {
        Serial.println("Error sending forecast");
      }
    }
  }

  // Dynamic Energy Pricing & Demand Response (third document)
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    if (line.startsWith("D:")) {
      float demand = line.substring(2).toFloat();
      demandData.type = 0; // Demand message
      demandData.demand.node_id = nodeID;
      demandData.demand.demand = demand;
      esp_err_t result = esp_now_send(node1Address, (uint8_t*)&demandData, sizeof(demandData));
      if (result == ESP_OK) {
        Serial.print("Sent Demand: ");
        Serial.print(demand);
        Serial.println(" kW");
      } else {
        Serial.println("Error sending demand");
      }
    }
    // Auction system net energy input (fourth document)
    else if (line.startsWith("NET_ENERGY:")) {
      net_energy = line.substring(11).toFloat();
    }
  }

  // Token System (fifth document)
  if (currentMillis - previousTokenMillis >= tokenInterval) {
    previousTokenMillis = currentMillis;
    float energyConsumed = Serial.parseFloat();
    if (energyConsumed > 0) {
      tokenData.energyKWh = energyConsumed;
      tokenData.transactionRequest = true;
      esp_err_t result = esp_now_send(node1Address, (uint8_t*)&tokenData, sizeof(tokenData));
      if (result == ESP_OK) {
        Serial.println("Token request sent with success");
      } else {
        Serial.println("Error sending token request");
      }
      tokenData.transactionRequest = false; // Reset
    }
  }

  // Real-Time Data Logging - Voltage (sixth document)
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    voltageData.voltage = line.toFloat();
    esp_err_t result = esp_now_send(node1Address, (uint8_t*)&voltageData, sizeof(voltageData));
    if (result == ESP_OK) {
      Serial.println("Voltage data sent to Node 1 with success");
    } else {
      Serial.println("Error sending voltage data");
    }
  }

  // Small delay to prevent watchdog timer issues
  delay(10);
}