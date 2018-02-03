/**
 * This sketch is a demonstration and calibration sketch for the Canique ULV board.
 * 
 * This sketch uses the library https://github.com/rocketscream/Low-Power
 * 
 */

#include <LowPower.h>
#include <avr/power.h>

#define LED_PIN 9
#define BATTREAD_ENABLE_PIN 4

#define ULV_MAX_MEASURE_VOLTAGE_3V3 //uncomment this if your ULV board can measure up to 3.3V
//#define ULV_MAX_MEASURE_VOLTAGE_1V8 //TODO uncomment this if your ULV board can measure up to 1.8V

/**
 * set prescaler for analog measurement depending on clock frequency
 * mhz / prescaler must be between 50 and 200 kHz
 */
#if F_CPU >= 8000000L
#define BATTERY_VOLTAGE_PRESCALER ((1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0)) //prescaler=128, used for >=8MHz
#else
#define BATTERY_VOLTAGE_PRESCALER ((1<<ADPS2)|(1<<ADPS1)|(1<<ADPS0)) //prescaler=64, used for 4MHz
#endif



/**
 * The variable VOLTAGE_CORRECTION can be used to calibrate the reading with a multimeter.
 * If it is set to 1.0 - no calibration is in effect
 * Calibration is recommended since
 * 1) the internal 1.1V reference of the Atmega is not perfectly precise
 * 2) the resistor divider on the Canique ULV Board uses 1% precision resistors so they can be 1% off
 * 
 * 
 * To calibrate your Canique ULV board:
 * 1) Read the battery voltage with a multimeter or similar precision device. Let's call the measured voltage V-MULTIMETER.
 * 2) Then run this sketch and see what this sketch will output. Let's call the output voltage V-ULVREADING.
 * 3) Finally set the variable VOLTAGE_CORRECTION to: V-MULTIMETER divided by V-ULVREADING
 * 
 * Example (with ~3.5% error offset while reading):
 * Multimeter reads 1.406V
 * Sketch outputs 1.357V (3.5% too low)
 * Set VOLTAGE_CORRECTION = 1.406 / 1.357;
 * 
 * After setting the variable, next time you upload the sketch, you will get the calibrated reading.
 */
const float VOLTAGE_CORRECTION = 1.0; //TODO calibrate


/**
 * R1 = 680kOhm 1% precision
 * R2 = 1MOhm 1% precision (for ULV boards supporting measurement up to 1.8V) OR 
 * R2 = 309kOhm 1% precision (for ULV boards supporting measurement up to 3.3V)
 * to calculate voltage from analog reading we need to do: (R1+R2) / R2
 */
#ifdef ULV_MAX_MEASURE_VOLTAGE_3V3
const float VOLTAGE_MULTIPLIER = VOLTAGE_CORRECTION * (989.0/309.0); //if your ULV board measures up to 3.3V
#else
const float VOLTAGE_MULTIPLIER = VOLTAGE_CORRECTION * (1680.0/1000.0); //if your ULV board measures up to 1.8V
#endif

/**
 * the time in microseconds that we wait after enabling battery voltage measurement, before actually measuring
 * we do this to let the voltage stabilize
 */
const uint16_t delayBeforeBattMeasureUs = 350;

/**
 * DO NOT REMOVE!
 * interrupt routine
 * needed for reading battery voltage in low noise mode
 */
ISR(ADC_vect) {
}


void setup() {
  //run @ 16MHz (not necessarily needed)
  clock_prescale_set(clock_div_1);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BATTREAD_ENABLE_PIN, OUTPUT);

  Serial.begin(115200);
  
  /**
   * use internal 1.1V reference voltage for measuring analog data like battery voltage
   * this means that any voltage we measure must not exceed 1.1V
   */
  ADMUX |= (1<<MUX2) | (1<<MUX1) | (1<<MUX0) | (1<<REFS1) | (1<<REFS0); //use channel ADC7, 1.1 voltage reference
  power_adc_disable(); //we turn off the analog to digital converter to save juice
}



/**
 * Reads a voltage from 0 to 1.1V on pin A7 by making multiple measurements and taking the average 
 * then calculates the original voltage before the resistor dividider that is connected to A7
 * 
 * returns measured voltage (before the resistor divider) in millivolts
 */
uint16_t getBatteryVoltage()
{
  digitalWrite(BATTREAD_ENABLE_PIN, HIGH); //activate measurement

  delayMicroseconds(delayBeforeBattMeasureUs); //wait before measuring
  
  /**
   * we will read a voltage between 0 and 1.1V
   */
  int voltagesADC = 0;
  const int numSamples = 10;

  power_adc_enable(); //activate analog to digital converter
  ADCSRA |= (1<<ADEN) | (1<<ADIE) | BATTERY_VOLTAGE_PRESCALER;

  //measure "numSamples" times
  for (byte i=0; i<numSamples; i++)
  {
    LowPower.adcNoiseReduction(SLEEP_15MS, ADC_ON, TIMER2_OFF); //TODO if you use timer2 meanwhile don't shut it down and leave TIMER2_ON!

    voltagesADC += ADC; //read value from analog input (between 0-1023 where 1023 would mean 1.1V)
  }

  power_adc_disable(); //deactivate analog to digital converter
  digitalWrite(BATTREAD_ENABLE_PIN, LOW); //deactivate measurement


  float avgVoltageADC = voltagesADC / (float)numSamples; //calculate average from measurements

  /**
   * calculate battery voltage based on resistor divider
   */
  uint16_t battVoltage = lround(1000 * ( 1.1 / 1023.0 ) * avgVoltageADC * VOLTAGE_MULTIPLIER);

  return battVoltage;
}


void loop() {
  uint16_t voltage = getBatteryVoltage();
  
  Serial.print("Canique ULV Board battery voltage: ");
  Serial.print(voltage);
  Serial.println(" mv");

  LowPower.powerDown(SLEEP_2S, ADC_OFF, BOD_OFF);
}
