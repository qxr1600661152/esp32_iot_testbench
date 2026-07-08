
#include <Arduino.h>

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("[s3-min] ALIVE — no EPD init");
}

void loop()
{
    Serial.println("[s3-min] tick");
    delay(1000);
}
