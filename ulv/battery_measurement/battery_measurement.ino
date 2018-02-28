/**
 * This sketch is a demonstration and calibration sketch for the Canique ULV board.
 * 
 * This sketch uses the library https://github.com/rocketscream/Low-Power
 * 
 */

#include <RFM69.h>
#include <LowPower.h>
#include <avr/power.h>

#define LED_PIN 9
#define BATTREAD_ENABLE_PIN 4

//#define ULV_MAX_MEASURE_VOLTAGE_3V3 //uncomment this if your ULV board can measure up to 3.3V
#define ULV_MAX_MEASURE_VOLTAGE_1V8 //TODO uncomment this if your ULV board can measure up to 1.8V

/**
 * set prescaler for analog measurement depending on clock frequency
 * mhz / prescaler must be between 50 and 200 kHz
 */
#if F_CPU >= 8000000L
#define BATTERY_VOLTAGE_PRESCALER ( _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0) ) //prescaler=128, used for >=8MHz
#else
#define BATTERY_VOLTAGE_PRESCALER ( _BV(ADPS2) | _BV(ADPS1) ) //prescaler=64, used for 4MHz
#endif

#define NODEID        0  //must be unique for each node on same network (range up to 254, 255 is used for broadcast)
#define NETWORKID     10  //the same on all nodes that talk to each other (range up to 255)
#define GATEWAYID     1

#define FREQUENCY   RF69_433MHZ
#define RADIO_HIGH_POWER   1 //comment out if using RFM69W

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

RFM69 radio;

/**
 * DO NOT REMOVE!
 * interrupt routine
 * needed for reading battery voltage in low noise mode
 * this method is called whenever an analog conversion has been done
 */
ISR(ADC_vect) {
}


void setup() {
  //run @ 16MHz (not necessarily needed)
  clock_prescale_set(clock_div_1);

  setupRadio();

  pinMode(LED_PIN, OUTPUT);
  pinMode(BATTREAD_ENABLE_PIN, OUTPUT);

  Serial.begin(115200);
  
  setupAnalogConverter();
}


void setupRadio()
{
  radio.initialize(FREQUENCY, NODEID, NETWORKID);

  #ifdef RADIO_HIGH_POWER
  radio.setHighPower(true);
  #endif
  
  //-1 dBm draws 16mA power (=powerlevels up to 17)
  //0 dBm draws 20mA power
  //13 dBm draws 45mA power
  //-18dBm + 5 dBm = -13 dBm
  radio.setPowerLevel(0); //0=minimum, 31=maximum TODO 15?

  //sleep right from the start
  radio.sleep();
}


void turnOffADC()
{
  ADCSRA &= ~_BV(ADEN); //turn off ADC enable bit
  power_adc_disable(); //we turn off the analog to digital converter to save juice
}


void setupAnalogConverter() {
  /**
   * use ADC7
   * use internal 1.1V reference voltage for measuring analog data like battery voltage
   * this means that any voltage we measure must not exceed 1.1V
   * Atmel ATmega328/P [DATASHEET] Atmel-42735B-ATmega328/P_Datasheet_Complete-11/2016, page 317
   */
  ADMUX |= _BV(MUX2) | _BV(MUX1) | _BV(MUX0) | _BV(REFS1) | _BV(REFS0); //use channel ADC7, 1.1 voltage reference
  turnOffADC();

  //according to https://meettechniek.info/embedded/arduino-analog.html it takes more than 5ms for the reference voltage to build up
  //that's why we put a 10ms delay here
  delay(10);
}


/**
 * Reads a voltage from 0 to 1.1V on pin A7 by making multiple measurements and taking the average 
 * then calculates the original voltage before the resistor dividider that is connected to A7
 * 
 * returns measured voltage (before the resistor divider) in millivolts, will return 0 
 * if noise reduction mode was disturbed by some interrupt during each measurement
 */
uint16_t getBatteryVoltage(byte numSamples)
{
  digitalWrite(BATTREAD_ENABLE_PIN, HIGH); //activate measurement

  delayMicroseconds(delayBeforeBattMeasureUs); //wait before measuring
  
  /**
   * we will read a voltage between 0 and 1.1V
   */
  int voltagesADC = 0;
  byte numValidSamples = 0;

  power_adc_enable(); //activate analog to digital converter

  /**
   * Atmel ATmega328/P [DATASHEET] Atmel-42735B-ATmega328/P_Datasheet_Complete-11/2016, page 319
   * enable analog digital converter
   * enable ADC interrupts
   * set prescaler
   */
  ADCSRA |= _BV(ADEN) | _BV(ADIE) | BATTERY_VOLTAGE_PRESCALER;

  //measure "numSamples" times
  for (byte i=0; i<numSamples; i++)
  {
    //TODO if you use timer2 meanwhile don't shut it down and leave TIMER2_ON but note that it 
    //might trigger interrupts disturbing the noise reduction mode!
    LowPower.adcNoiseReduction(SLEEP_15MS, ADC_ON, TIMER2_OFF);

    /**
     * if we exit from the noise reduction sleep mode, we should have a reading in ADC, but it might be that we exited too early because of
     * some other interrupt (e.g. pin change or timer2... see page 63 of Atmega328P documentation)
     * that's why we check if we have a reading...(ADSC will be 0 if we have a reading)
     */
    if ( (ADCSRA & _BV(ADSC)) != 0) //skip reading analog voltage if we got interrupted too early
    {
      continue;
    }

    voltagesADC += ADC; //read value from analog input (between 0-1023 where 1023 would mean 1.1V)
    numValidSamples++;
  }

  turnOffADC(); //deactivate analog to digital converter
  digitalWrite(BATTREAD_ENABLE_PIN, LOW); //deactivate measurement

  if (numValidSamples == 0)
  {
    return 0;
  }
  
  float avgVoltageADC = voltagesADC / (float)numValidSamples; //calculate average from measurements

  /**
   * calculate battery voltage based on resistor divider
   */
  uint16_t battVoltage = lround(1000 * ( 1.1 / 1023.0 ) * avgVoltageADC * VOLTAGE_MULTIPLIER);

  return battVoltage;
}


void loop() {
  uint16_t voltage = getBatteryVoltage(10); //take the average of 10 samples
  
  Serial.print("Canique ULV Board battery voltage: ");
  Serial.print(voltage);
  Serial.println(" mv");

  /**
   * we switched ADC off anyway in getBatteryVoltage() so it is off now
   * that's why we leave ADC untouched (=ADC_ON, that does not change anything)
   * if we used ADC_OFF here, on next wakeup ADC would be on again, @see rocketscream code
   */
  LowPower.powerDown(SLEEP_8S, ADC_ON, BOD_OFF);
  //LowPower.powerDown(SLEEP_8S, ADC_ON, BOD_OFF);
}
