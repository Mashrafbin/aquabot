#include <Arduino.h>

int pH_Value;
float Voltage;

const int PH_PIN = 34;

void setup()
{
    Serial.begin(115200);
    pinMode(PH_PIN, INPUT);
}

void loop()
{
    pH_Value = analogRead(PH_PIN);

    Voltage = pH_Value * (3.3 / 4095.0);

    Serial.println(Voltage, 3);

    delay(100);
}