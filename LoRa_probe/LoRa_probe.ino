#include <EEPROM.h>
#include <SPI.h>
#include <LoRa.h>

#include <Adafruit_BME680.h>
#include <bme68x.h>
#include <bme68x_defs.h>
#include <Wire.h>
#include "AS5600.h"
#include "soc/rtc.h"
#include "driver/rtc_io.h"

#include "common.h"

#define WAKEUP_GPIO GPIO_NUM_15
#define USE_EXT0_WAKEUP 1  

Adafruit_BME680 bme;
AS5600 as5600;

unsigned int wind_clk = 0;
RTC_DATA_ATTR unsigned int tip_clk;
RTC_DATA_ATTR uint64_t last_timer_triggered = 0;

const float DEG2RAD = 3.1415926535898/180;
const float RAD2DEG = 1/DEG2RAD;

char *identifier PROGMEM = "0001";
char *format_scheme PROGMEM = "I%s T%04.1f H%04.1f P%04i W%03u%05u G%03u%05u R%03u";

void blinkLED(int count = 3) {
  for (int k = 0; k < count; k++) {
    digitalWrite(2, HIGH);
    delay(250);
    digitalWrite(2, LOW);
    delay(250);
  }
}

void isrWind() {
  wind_clk ++;
}

void isrTip() {
  tip_clk ++;
}

void setup() {
  //get wakeup reason
  Serial.begin(115200);
  unsigned long long int time_to_sleep = 1 * 60 * 1000000;
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if ((wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) and (uint64_t)(rtc_time_get() - last_timer_triggered) < (time_to_sleep - 30*1000000)) 
  {
    tip_clk ++;
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 1);
    rtc_gpio_pullup_dis(WAKEUP_GPIO);
    rtc_gpio_pulldown_en(WAKEUP_GPIO);
    Serial.print("Time yet to sleep: "); Serial.println(time_to_sleep - (uint64_t)(rtc_time_get() - last_timer_triggered));
    esp_sleep_enable_timer_wakeup(time_to_sleep - (uint64_t)(rtc_time_get() - last_timer_triggered));
    esp_deep_sleep_start();  
  }
  
  esp_sleep_enable_timer_wakeup(time_to_sleep);
  //Serial startup
  
  pinMode(2, OUTPUT);
  // Blink led for indication
  blinkLED(2);
  Serial.println("LoRa Sender");
  SPIClass * vspi = NULL; // throw predefined vspi out of the window :D
  vspi = new SPIClass(VSPI); // create a new one
  vspi->begin(SCK, MISO, MOSI, NSS); // assign custom pins
  LoRa.setSPI(*vspi); // tell LORA to use this custom spi
  LoRa.setPins(NSS, RST, DIO); // NSS RESET DIO0
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1) {
      blinkLED(1);
      delay(5000);
      }
  }
  //LoRa started up, setting up parameters
  LoRa.setSpreadingFactor(SF);
  LoRa.setSignalBandwidth(BW);
  LoRa.setCodingRate4(CDRATE);
  LoRa.setPreambleLength(15);
  LoRa.setSyncWord(0x35);
  //Starting BME680
  //Setting up I2C bus
  Wire.begin(SDA,SCL);
  //Checking BME680
  if (!bme.begin()) {
  Serial.println("BME680 not online, check wiring!");
  while (1)
  {
   blinkLED(2);
   delay(5000); 
  }
  }
  //Setting params for BME680
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
  //Set up AS5600
  as5600.begin();
  if (!as5600.isConnected()) {
    Serial.println("AS5600 not online");
    while (1) 
    {
      blinkLED(3);
      delay(5000);
    }
    }
  //Set up wind speed sensor
  pinMode(WIND_SPD, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WIND_SPD),isrWind,CHANGE);

  //Set up hall sensor
  pinMode(TIP_BUCKET, INPUT);
  attachInterrupt(digitalPinToInterrupt(TIP_BUCKET),isrTip,RISING);

  //everything set up, measuring during 2 min
  unsigned int wind_dir[120];
  unsigned int wind_speed_clk[120]; 
  float temp;
  float humidity;
  float raw_pressure;
  wind_clk = 0;
  delay(1000);
  float wind_speed_avg;
  float wind_speed_max = 0.0;
  for (int i=0; i < 120; i++) 
  {
    //temp, humidity, pressure (divided by 120 for ignoring average calculation
    bme.performReading();
    temp = temp + bme.temperature / 120.0;
    humidity = humidity + bme.humidity / 120.0;
    raw_pressure = raw_pressure + (bme.pressure / 100.0 / 120.0);
    //wind speed and direction (2 min whole record)
    wind_dir[i] = round(as5600.readAngle() * AS5600_RAW_TO_DEGREES);
    wind_speed_clk[i] = wind_clk;
    wind_clk = 0;
    wind_speed_avg = wind_speed_avg + (float)(wind_speed_clk[i]) / 120.0;
    if (wind_speed_max < wind_speed_clk[i]) 
    {
      wind_speed_max = (float)(wind_speed_clk[i]);
    }
    delay(1000); //wait 1 sec for each sample
  }
  //calculating standard deviation for wind speed
  float variance = 0;
  for (int i=0; i < 120; i++) 
  {
    variance = variance + pow(((float)(wind_speed_clk[i]) - wind_speed_avg),2);
  }
  float std_deviation = sqrt(variance);
  //calculating z-score for each wind speed clk value, and determine gust speed
  float maintain_wind_speed = 0.0;
  float maintain_wind_vec_x = 0.0;
  float maintain_wind_vec_y = 0.0;
  int gust_wind_dir = 0;
  for (int i=0; i < 120; i++) 
  {
    float z_score = ((float)(wind_speed_clk[i]) - wind_speed_avg)/std_deviation;
    if (z_score <= 1.44) //can be considered as normal wind speed
    {
      maintain_wind_speed = maintain_wind_speed + (float)(wind_speed_clk[i]) / 120.0;
      maintain_wind_vec_x = maintain_wind_vec_x + (cos( (float)(wind_dir[i]) * DEG2RAD)) / 120.0;
      maintain_wind_vec_y = maintain_wind_vec_y + (sin( (float)(wind_dir[i]) * DEG2RAD)) / 120.0;
    }
    if (wind_speed_clk[i] - wind_speed_max < 0.00001) 
    {
      gust_wind_dir = wind_dir[i];
    }
  }
  float maintain_wind_dir = atan2(maintain_wind_vec_y,maintain_wind_vec_x)*RAD2DEG;
  if (maintain_wind_dir < 0) 
  {
    maintain_wind_dir = maintain_wind_dir + 360;
  }
  //every data ready: BME680 (temp, humid, pres), tip bucket (tip_clk), wind.
  //Formatting and sending data via LoRa, also  show on serial for debugging
  char buf[48];
  sprintf(buf,format_scheme,identifier, temp, humidity, (int)(round(raw_pressure)), (int)(round(maintain_wind_dir)), (int)(round(maintain_wind_speed)), gust_wind_dir, wind_speed_max, tip_clk);
  tip_clk = 0;
  //Sending data
  Serial.println(buf);
  Serial.flush();
  LoRa.beginPacket();
  LoRa.print(buf);
  LoRa.endPacket();
  //deep sleep
  esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);
  rtc_gpio_hold_en();
  last_timer_triggered = rtc_time_get();
  esp_deep_sleep_start();  
}

void loop() {
  // put your main code here, to run repeatedly:

}
