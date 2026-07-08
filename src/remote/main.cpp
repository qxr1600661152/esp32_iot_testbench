
#include <Arduino.h>
#include "driver/gpio.h"
#include "pins.h"

__attribute__((constructor(101))) static void c3_release_s3_early(void)
{
    gpio_reset_pin((gpio_num_t)S3_EN_GPIO);
    gpio_set_direction((gpio_num_t)S3_EN_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)S3_EN_GPIO, 1);
}

static void releaseS3En()
{
    pinMode(S3_EN_GPIO, OUTPUT);
    digitalWrite(S3_EN_GPIO, HIGH);
}

static void pulseS3Reset()
{
    digitalWrite(S3_EN_GPIO, LOW);
    delay(80);
    digitalWrite(S3_EN_GPIO, HIGH);
    Serial.println("[c3] S3 EN pulse reset done");
}

static void pollSerial()
{
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line == "S3_RESET") {
                pulseS3Reset();
            } else if (line == "S3_RELEASE") {
                releaseS3En();
                Serial.println("[c3] S3 EN held HIGH");
            } else if (line == "STATUS") {
                int ready = digitalRead(S3_READY_GPIO);
                Serial.printf("[c3] EN=HIGH ready_gpio%d=%d\n", S3_READY_GPIO, ready);
            }
            line = "";
        } else if (line.length() < 32) {
            line += c;
        }
    }
}

void setup()
{
    releaseS3En();
    pinMode(S3_READY_GPIO, INPUT);
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[c3] boot — S3 EN released (GPIO5=HIGH)");
    Serial.println("[c3] cmds: S3_RESET | S3_RELEASE | STATUS");
}

void loop()
{
    digitalWrite(S3_EN_GPIO, HIGH);
    pollSerial();
    delay(50);
}
