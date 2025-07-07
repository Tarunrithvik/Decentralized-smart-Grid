#define SENSOR_PIN A0  // Voltage sensor pin
#define VREF 5.0       // Reference voltage (5V)
#define ADC_RES 1023.0 // 10-bit ADC resolution

void setup() {

  Serial.begin(9600);
}


void loop() {
    int sensorValue = analogRead(SENSOR_PIN);
    float voltage = (sensorValue * VREF) / ADC_RES;  // Convert ADC to voltage

    Serial.println(voltage);

    delay(1500);  // Send data every second
}
