#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_now.h>

// WiFi credentials for Access Point
const char* ssid = "One Plus Nord Ce4";
const char* password = "123456987";

// MAC Addresses (replace with actual addresses)

uint8_t broadcastAddress[] = {0xF0 , 0x24 , 0xF9 , 0x5A, 0xD0 ,0x74}; // Node 1 (central node)
uint8_t node2Address[] = {0x34, 0x5F, 0x45, 0xA7, 0xE9, 0x24}; // Node 2 (this device, placeholder)
uint8_t node3Address[] = {0xE4, 0x65, 0xB8, 0xE7, 0x5A, 0xEC}; // Node 3
uint8_t node4Address[] = {0x10, 0x06, 0x1C, 0xB5, 0x09, 0x64};// Node 4

// Unified message structure
typedef struct {
    uint8_t type; // 0: Voltage, 1: Forecast, 2: Pricing/Demand, 3: Auction, 4: Trading, 5: Logging
    uint8_t data[50];
} message_t;

// *** Data Structures for Each Feature ***

// Voltage Receiver (First Document)
typedef struct {
    float voltage; // Previously 'c'
} voltage_msg;

// Forecast Receiver (Second Document)
typedef struct {
    float forecast;
} forecast_msg;

// Pricing & Demand Response (Third Document)
typedef struct {
    uint8_t subtype; // 0: Demand, 1: Price, 2: Command
    union {
        struct {
            uint8_t node_id;
            float demand;
        } demand;
        struct {
            float price;
        } price;
        struct {
            uint8_t command; // 0: Normal, 1: Reduce
        } command;
    };
} pricing_msg;

// Auction System (Fourth Document)
#define MSG_AUCTION_START 0
#define MSG_BID 1
#define MSG_TRANSACTION_SELL 2
#define MSG_TRANSACTION_BUY 3
typedef struct {
    uint8_t subtype; // MSG_AUCTION_START, MSG_BID, etc.
    uint8_t node_id;
    uint8_t action; // 0: Sell, 1: Buy
    float energy_amount;
    float price_per_unit;
} auction_bid_msg;

typedef struct {
    uint8_t subtype;
    uint8_t buyer_id;
    float energy_amount;
    float price_per_unit;
} transaction_sell_msg;

typedef struct {
    uint8_t subtype;
    uint8_t seller_id;
    float energy_amount;
    float price_per_unit;
} transaction_buy_msg;

// Trading System (Fifth Document)
typedef struct {
    uint8_t nodeID; // 1: Central, 2: Producer, 3: Consumer, 4: Monitor
    float energyKWh;
    float tokenAmount;
    bool transactionRequest;
} trading_msg;

// Logging & Analytics (Sixth Document)
typedef struct {
    int nodeID;
    float voltage;
} logging_msg;

// *** Global Variables ***

// Pricing & Demand Response
float demand2 = 0.0, demand3 = 0.0, demand4 = 0.0;
float total_demand = 0.0;
float price = 5.0; // Base price in INR/kWh
bool high_demand = false;

// Trading System
float tokenBalances[4] = {1000.0, 500.0, 500.0, 500.0}; // Nodes 1-4
#define MAX_TRANSACTIONS 10
struct Transaction {
    unsigned long timestamp;
    uint8_t nodeID;
    float energyKWh;
    float tokensTransferred;
    bool isProducer;
};
Transaction transactionLog[MAX_TRANSACTIONS];
int transactionIndex = 0;

// Auction System
uint8_t node_mac[3][6];
bool node_registered[3] = {false, false, false};
auction_bid_msg bids[3];
bool bid_received[3] = {false, false, false};
unsigned long last_auction_time = 0;
const unsigned long auction_interval = 10000; // 10s
const unsigned long bid_timeout = 2000; // 2s

// Forecasting
float forecastNode2 = 0.0, forecastNode3 = 0.0, forecastNode4 = 0.0;

// Logging & Analytics
float voltages[5] = {0}; // Index 0 unused
float averageVoltage = 0.0;
String lowVoltageWarnings = "";
unsigned long last_logging_time = 0;
const unsigned long logging_interval = 2000; // 2s

// Web Dashboard
AsyncWebServer server(80);
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Smart Grid Dashboard</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; margin: 0; padding: 20px; }
    .section { border: 1px solid #ccc; padding: 10px; margin-bottom: 10px; }
    h2 { margin-top: 0; }
    ul { list-style-type: none; padding: 0; }
  </style>
</head>
<body>
  <h1>Smart Grid Dashboard</h1>
  <div class="section">
    <h2>Dynamic Energy Pricing & Demand Response</h2>
    <p>Current Price: <span id="price"></span> INR/kWh</p>
    <p>Total Demand: <span id="total_demand"></span> kW</p>
    <p>Demand Response: <span id="demand_response"></span></p>
  </div>
  <div class="section">
    <h2>Energy Trading & Token System</h2>
    <p>Token Balances:</p>
    <ul>
      <li>Node 1: <span id="token_balance_node1"></span> INR</li>
      <li>Node 2: <span id="token_balance_node2"></span> INR</li>
      <li>Node 3: <span id="token_balance_node3"></span> INR</li>
      <li>Node 4: <span id="token_balance_node4"></span> INR</li>
    </ul>
    <p>Recent Transactions:</p>
    <ul id="transaction_list"></ul>
  </div>
  <div class="section">
    <h2>Energy Auction System</h2>
    <p>Last Auction Start Time: <span id="auction_start_time"></span> ms</p>
    <p>Bids Received:</p>
    <ul id="bid_list"></ul>
  </div>
  <div class="section">
    <h2>AI-Powered Demand Forecasting</h2>
    <p>Node 2 Forecast: <span id="forecast_node2"></span> W</p>
    <p>Node 3 Forecast: <span id="forecast_node3"></span> W</p>
    <p>Node 4 Forecast: <span id="forecast_node4"></span> W</p>
  </div>
  <div class="section">
    <h2>Real-Time Data Logging & Analytics</h2>
    <p>Voltages:</p>
    <ul>
      <li>Node 1: <span id="voltage_node1"></span> V</li>
      <li>Node 2: <span id="voltage_node2"></span> V</li>
      <li>Node 3: <span id="voltage_node3"></span> V</li>
      <li>Node 4: <span id="voltage_node4"></span> V</li>
    </ul>
    <p>Average Voltage: <span id="average_voltage"></span> V</p>
    <p>Warnings: <span id="warnings"></span></p>
  </div>
  <script>
    function updateDashboard() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('price').textContent = data.pricing.price.toFixed(2);
          document.getElementById('total_demand').textContent = data.pricing.total_demand.toFixed(2);
          document.getElementById('demand_response').textContent = data.pricing.demand_response;
          document.getElementById('token_balance_node1').textContent = data.trading.tokenBalances[0].toFixed(2);
          document.getElementById('token_balance_node2').textContent = data.trading.tokenBalances[1].toFixed(2);
          document.getElementById('token_balance_node3').textContent = data.trading.tokenBalances[2].toFixed(2);
          document.getElementById('token_balance_node4').textContent = data.trading.tokenBalances[3].toFixed(2);
          const transactionList = document.getElementById('transaction_list');
          transactionList.innerHTML = '';
          data.trading.transactions.forEach(tx => {
            const li = document.createElement('li');
            li.textContent = `Time: ${tx.timestamp} ms, Node ${tx.nodeID}: ${tx.isProducer ? 'Sold' : 'Bought'} ${tx.energyKWh.toFixed(2)} kWh for ${tx.tokensTransferred.toFixed(2)} INR`;
            transactionList.appendChild(li);
          });
          document.getElementById('auction_start_time').textContent = data.auction.start_time;
          const bidList = document.getElementById('bid_list');
          bidList.innerHTML = '';
          data.auction.bids.forEach(bid => {
            const li = document.createElement('li');
            li.textContent = `Node ${bid.node_id}: ${bid.action == 0 ? 'Sell' : 'Buy'} ${bid.energy_amount.toFixed(2)} units at ${bid.price_per_unit.toFixed(2)} INR/unit`;
            bidList.appendChild(li);
          });
          document.getElementById('forecast_node2').textContent = data.forecasting.forecastNode2.toFixed(2);
          document.getElementById('forecast_node3').textContent = data.forecasting.forecastNode3.toFixed(2);
          document.getElementById('forecast_node4').textContent = data.forecasting.forecastNode4.toFixed(2);
          document.getElementById('voltage_node1').textContent = data.logging.voltages[0].toFixed(2);
          document.getElementById('voltage_node2').textContent = data.logging.voltages[1].toFixed(2);
          document.getElementById('voltage_node3').textContent = data.logging.voltages[2].toFixed(2);
          document.getElementById('voltage_node4').textContent = data.logging.voltages[3].toFixed(2);
          document.getElementById('average_voltage').textContent = data.logging.averageVoltage.toFixed(2);
          document.getElementById('warnings').textContent = data.logging.lowVoltageWarnings || 'None';
        });
    }
    setInterval(updateDashboard, 5000);
    updateDashboard();
  </script>
</body>
</html>
)rawliteral";

// *** Helper Functions ***

void sendPrice(float price_val) {
    message_t msg;
    msg.type = 2; // Pricing
    pricing_msg pm;
    pm.subtype = 1; // Price
    pm.price.price = price_val;
    memcpy(msg.data, &pm, sizeof(pricing_msg));
    esp_now_send(node2Address, (uint8_t*)&msg, sizeof(message_t));
    esp_now_send(node3Address, (uint8_t*)&msg, sizeof(message_t));
    esp_now_send(node4Address, (uint8_t*)&msg, sizeof(message_t));
}

void sendCommand(uint8_t node_id, uint8_t command) {
    message_t msg;
    msg.type = 2; // Pricing
    pricing_msg pm;
    pm.subtype = 2; // Command
    pm.command.command = command;
    memcpy(msg.data, &pm, sizeof(pricing_msg));
    if (node_id == 2) esp_now_send(node2Address, (uint8_t*)&msg, sizeof(message_t));
    else if (node_id == 3) esp_now_send(node3Address, (uint8_t*)&msg, sizeof(message_t));
}

void sendAuctionStart() {
    message_t msg;
    msg.type = 3; // Auction
    auction_bid_msg abm;
    abm.subtype = MSG_AUCTION_START;
    memcpy(msg.data, &abm, sizeof(auction_bid_msg));
    esp_now_send(broadcastAddress, (uint8_t*)&msg, sizeof(message_t));
}

void resolveAuction() {
    auction_bid_msg sell_bids[3];
    int sell_count = 0;
    auction_bid_msg buy_bids[3];
    int buy_count = 0;

    for (int i = 0; i < 3; i++) {
        if (bid_received[i]) {
            if (bids[i].action == 0) sell_bids[sell_count++] = bids[i];
            else if (bids[i].action == 1) buy_bids[buy_count++] = bids[i];
            bid_received[i] = false;
        }
    }

    for (int i = 0; i < sell_count - 1; i++)
        for (int j = 0; j < sell_count - i - 1; j++)
            if (sell_bids[j].price_per_unit > sell_bids[j + 1].price_per_unit) {
                auction_bid_msg temp = sell_bids[j];
                sell_bids[j] = sell_bids[j + 1];
                sell_bids[j + 1] = temp;
            }

    for (int i = 0; i < buy_count - 1; i++)
        for (int j = 0; j < buy_count - i - 1; j++)
            if (buy_bids[j].price_per_unit < buy_bids[j + 1].price_per_unit) {
                auction_bid_msg temp = buy_bids[j];
                buy_bids[j] = buy_bids[j + 1];
                buy_bids[j + 1] = temp;
            }

    int sell_idx = 0, buy_idx = 0;
    while (sell_idx < sell_count && buy_idx < buy_count) {
        auction_bid_msg& sell = sell_bids[sell_idx];
        auction_bid_msg& buy = buy_bids[buy_idx];
        if (buy.price_per_unit >= sell.price_per_unit) {
            float amount = (sell.energy_amount < buy.energy_amount) ? sell.energy_amount : buy.energy_amount;

            message_t msg_sell;
            msg_sell.type = 3;
            transaction_sell_msg ts_msg;
            ts_msg.subtype = MSG_TRANSACTION_SELL;
            ts_msg.buyer_id = buy.node_id;
            ts_msg.energy_amount = amount;
            ts_msg.price_per_unit = sell.price_per_unit;
            memcpy(msg_sell.data, &ts_msg, sizeof(transaction_sell_msg));
            esp_now_send(node_mac[sell.node_id - 2], (uint8_t*)&msg_sell, sizeof(message_t));

            message_t msg_buy;
            msg_buy.type = 3;
            transaction_buy_msg tb_msg;
            tb_msg.subtype = MSG_TRANSACTION_BUY;
            tb_msg.seller_id = sell.node_id;
            tb_msg.energy_amount = amount;
            tb_msg.price_per_unit = sell.price_per_unit;
            memcpy(msg_buy.data, &tb_msg, sizeof(transaction_buy_msg));
            esp_now_send(node_mac[buy.node_id - 2], (uint8_t*)&msg_buy, sizeof(message_t));

            sell.energy_amount -= amount;
            buy.energy_amount -= amount;
            if (sell.energy_amount <= 0.001) sell_idx++;
            if (buy.energy_amount <= 0.001) buy_idx++;
        } else break;
    }
}

// *** Callbacks ***

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Last Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    message_t msg;
    memcpy(&msg, incomingData, sizeof(msg));
    const uint8_t *mac = recv_info->src_addr;

    switch (msg.type) {
        case 0: { // Voltage Receiver
            voltage_msg vm;
            memcpy(&vm, msg.data, sizeof(voltage_msg));
            Serial.print("Voltage in V: ");
            Serial.println(vm.voltage);
            break;
        }
        case 1: { // Forecast Receiver
            forecast_msg fm;
            memcpy(&fm, msg.data, sizeof(forecast_msg));
            if (memcmp(mac, node2Address, 6) == 0) {
                forecastNode2 = fm.forecast;
                Serial.print("Forecast from Node 2 (W): ");
                Serial.println(fm.forecast);
            } else if (memcmp(mac, node3Address, 6) == 0) {
                forecastNode3 = fm.forecast;
                Serial.print("Forecast from Node 3 (W): ");
                Serial.println(fm.forecast);
            } else if (memcmp(mac, node4Address, 6) == 0) {
                forecastNode4 = fm.forecast;
                Serial.print("Forecast from Node 4 (W): ");
                Serial.println(fm.forecast);
            }
            break;
        }
        case 2: { // Pricing & Demand Response
            pricing_msg pm;
            memcpy(&pm, msg.data, sizeof(pricing_msg));
            if (pm.subtype == 0) { // Demand
                if (pm.demand.node_id == 2) demand2 = pm.demand.demand;
                else if (pm.demand.node_id == 3) demand3 = pm.demand.demand;
                else if (pm.demand.node_id == 4) demand4 = pm.demand.demand;
                total_demand = demand2 + demand3 + demand4;
                price = 5.0 + 0.1 * total_demand;
                Serial.print("Total Demand: "); Serial.print(total_demand); Serial.print(" kW, Price: "); Serial.print(price); Serial.println(" INR/kWh");
                sendPrice(price);
                if (total_demand > 10.0 && !high_demand) {
                    sendCommand(2, 1);
                    sendCommand(3, 1);
                    high_demand = true;
                    Serial.println("High demand detected, sent reduce commands");
                } else if (total_demand < 8.0 && high_demand) {
                    sendCommand(2, 0);
                    sendCommand(3, 0);
                    high_demand = false;
                    Serial.println("Demand normalized, sent normal commands");
                }
            }
            break;
        }
        case 3: { // Auction System
            auction_bid_msg abm;
            memcpy(&abm, msg.data, sizeof(auction_bid_msg));
            if (abm.subtype == MSG_BID) {
                int index = abm.node_id - 2;
                if (index >= 0 && index < 3) {
                    bids[index] = abm;
                    bid_received[index] = true;
                    if (!node_registered[index]) {
                        memcpy(node_mac[index], mac, 6);
                        node_registered[index] = true;
                        esp_now_peer_info_t peerInfo = {};
                        memcpy(peerInfo.peer_addr, mac, 6);
                        peerInfo.channel = 0;
                        peerInfo.encrypt = false;
                        esp_now_add_peer(&peerInfo);
                    }
                }
            }
            break;
        }
        case 4: { // Trading System
            trading_msg tm;
            memcpy(&tm, msg.data, sizeof(trading_msg));
            if (tm.transactionRequest) {
                if (tm.nodeID == 2) {
                    float tokensEarned = tm.energyKWh * 10.0;
                    tokenBalances[1] += tokensEarned;
                    tokenBalances[0] -= tokensEarned;
                    trading_msg response = {1, 0, tokensEarned, false};
                    message_t msg_resp;
                    msg_resp.type = 4;
                    memcpy(msg_resp.data, &response, sizeof(trading_msg));
                    esp_now_send(node2Address, (uint8_t*)&msg_resp, sizeof(message_t));
                    transactionLog[transactionIndex] = {millis(), tm.nodeID, tm.energyKWh, tokensEarned, true};
                    transactionIndex = (transactionIndex + 1) % MAX_TRANSACTIONS;
                } else if (tm.nodeID == 3) {
                    float tokensSpent = tm.energyKWh * 10.0;
                    if (tokenBalances[2] >= tokensSpent) {
                        tokenBalances[2] -= tokensSpent;
                        tokenBalances[0] += tokensSpent;
                        trading_msg response = {1, 0, tokensSpent, false};
                        message_t msg_resp;
                        msg_resp.type = 4;
                        memcpy(msg_resp.data, &response, sizeof(trading_msg));
                        esp_now_send(node3Address, (uint8_t*)&msg_resp, sizeof(message_t));
                        transactionLog[transactionIndex] = {millis(), tm.nodeID, tm.energyKWh, tokensSpent, false};
                        transactionIndex = (transactionIndex + 1) % MAX_TRANSACTIONS;
                    }
                }
            }
            break;
        }
        case 5: { // Logging & Analytics
            logging_msg lm;
            memcpy(&lm, msg.data, sizeof(logging_msg));
            if (lm.nodeID >= 1 && lm.nodeID <= 4) {
                voltages[lm.nodeID] = lm.voltage;
                Serial.print("Received from Node ");
                Serial.print(lm.nodeID);
                Serial.print(" - Voltage: ");
                Serial.print(lm.voltage);
                Serial.println(" V");
                if (lm.voltage < 2.0) {
                    lowVoltageWarnings += "Node " + String(lm.nodeID) + " has low voltage; ";
                    Serial.print("WARNING: Low voltage detected at Node ");
                    Serial.println(lm.nodeID);
                }
                float sum = 0;
                int count = 0;
                for (int i = 1; i <= 4; i++) {
                    if (voltages[i] > 0) {
                        sum += voltages[i];
                        count++;
                    }
                }
                if (count > 0) {
                    averageVoltage = sum / count;
                    Serial.print("Average Voltage Across Grid: ");
                    Serial.print(averageVoltage);
                    Serial.println(" V");
                }
            }
            break;
        }
    }
}

// *** Setup and Loop ***

void setup() {
    Serial.begin(115200);

    // Initialize WiFi as AP and STA
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ssid, password);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    // Register peers
    esp_now_peer_info_t peerInfo = {};
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, node2Address, 6);
    esp_now_add_peer(&peerInfo);
    memcpy(peerInfo.peer_addr, node3Address, 6);
    esp_now_add_peer(&peerInfo);
    memcpy(peerInfo.peer_addr, node4Address, 6);
    esp_now_add_peer(&peerInfo);
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    esp_now_add_peer(&peerInfo);

    // Web Server Routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{";
        json += "\"pricing\":{\"price\":" + String(price) + ",\"total_demand\":" + String(total_demand) + ",\"demand_response\":\"" + String(high_demand ? "Reduce Load" : "Normal") + "\"},";
        json += "\"trading\":{\"tokenBalances\":[" + String(tokenBalances[0]) + "," + String(tokenBalances[1]) + "," + String(tokenBalances[2]) + "," + String(tokenBalances[3]) + "],";
        json += "\"transactions\":[";
        bool first = true;
        for (int i = 0; i < MAX_TRANSACTIONS; i++) {
            int idx = (transactionIndex + i) % MAX_TRANSACTIONS;
            if (transactionLog[idx].timestamp > 0) {
                if (!first) json += ",";
                json += "{\"timestamp\":" + String(transactionLog[idx].timestamp) + ",\"nodeID\":" + String(transactionLog[idx].nodeID) + ",\"energyKWh\":" + String(transactionLog[idx].energyKWh) + ",\"tokensTransferred\":" + String(transactionLog[idx].tokensTransferred) + ",\"isProducer\":" + String(transactionLog[idx].isProducer ? "true" : "false") + "}";
                first = false;
            }
        }
        json += "]},";
        json += "\"auction\":{\"start_time\":" + String(last_auction_time) + ",\"bids\":[";
        first = true;
        for (int i = 0; i < 3; i++) {
            if (bid_received[i]) {
                if (!first) json += ",";
                json += "{\"node_id\":" + String(bids[i].node_id) + ",\"action\":" + String(bids[i].action) + ",\"energy_amount\":" + String(bids[i].energy_amount) + ",\"price_per_unit\":" + String(bids[i].price_per_unit) + "}";
                first = false;
            }
        }
        json += "]},";
        json += "\"forecasting\":{\"forecastNode2\":" + String(forecastNode2) + ",\"forecastNode3\":" + String(forecastNode3) + ",\"forecastNode4\":" + String(forecastNode4) + "},";
        json += "\"logging\":{\"voltages\":[" + String(voltages[1]) + "," + String(voltages[2]) + "," + String(voltages[3]) + "," + String(voltages[4]) + "],\"averageVoltage\":" + String(averageVoltage) + ",\"lowVoltageWarnings\":\"" + lowVoltageWarnings + "\"}";
        json += "}";
        request->send(200, "application/json", json);
    });

    server.begin();
}

void loop() {
    unsigned long current_time = millis();

    // Logging Node 1 Voltage
    if (current_time - last_logging_time >= logging_interval) {
        last_logging_time = current_time;
        float ownVoltage = analogRead(34) * (3.3 / 4095.0);
        voltages[1] = ownVoltage;
        Serial.print("Node 1 - Voltage: ");
        Serial.print(ownVoltage);
        Serial.println(" V");
        if (ownVoltage < 2.0) {
            lowVoltageWarnings += "Node 1 has low voltage; ";
            Serial.println("WARNING: Low voltage detected at Node 1");
        }
        float sum = 0;
        int count = 0;
        for (int i = 1; i <= 4; i++) {
            if (voltages[i] > 0) {
                sum += voltages[i];
                count++;
            }
        }
        if (count > 0) {
            averageVoltage = sum / count;
            Serial.print("Average Voltage Across Grid: ");
            Serial.print(averageVoltage);
            Serial.println(" V");
        }
    }

    // Auction System
    if (current_time - last_auction_time >= auction_interval) {
        last_auction_time = current_time;
        sendAuctionStart();
        Serial.println("Sent AUCTION_START");
        delay(bid_timeout);
        resolveAuction();
    }

    // Trading Update to Node 4
    static unsigned long last_trading_time = 0;
    if (current_time - last_trading_time >= 5000) {
        last_trading_time = current_time;
        trading_msg tm = {1, 0.0, tokenBalances[0], false};
        message_t msg;
        msg.type = 4;
        memcpy(msg.data, &tm, sizeof(trading_msg));
        esp_now_send(node4Address, (uint8_t*)&msg, sizeof(message_t));
    }

    delay(100); // Prevent tight looping
}