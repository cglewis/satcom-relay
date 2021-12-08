#include "satcom-relay.h"

SATCOMRelay relay;

uint32_t gpsTimer, testModePrintTimer, batteryCheckTimer = 2000000000L; // Make all of these times far in the past by setting them near the middle of the millis() range so they are checked promptly
volatile uint32_t awakeTimer = 0;
String msg;


#define interruptPin 15

unsigned long timeDiff(unsigned long x, unsigned long nowTime) {
  if (nowTime >= x) {
    return nowTime - x;
  }
  return (ULONG_MAX - x) + nowTime;
}

unsigned long nowTimeDiff(unsigned long x) {
  return timeDiff(x, millis());
}

void setup() {
  while(!Serial);
  Serial.begin(115200);

  // RF connection
  Serial1.begin(115200);

  relay.gps.initGPS();

  // Setup interrupt sleep pin
  setupInterruptSleep();
}

void loop() {
  rfCheck();
  gpsCheck();
  batteryCheck();
  sleepCheck();

  #if TEST_MODE // print the state of the relay
  if (nowTimeDiff(testModePrintTimer) > TEST_MODE_PRINT_INTERVAL) {
    testModePrintTimer = millis(); // reset the timer
    relay.print();
    Serial.println();
  }
  #endif
}

void setupInterruptSleep() {
  // whenever we get an interrupt, reset the awake clock.
  attachInterrupt(digitalPinToInterrupt(interruptPin), EIC_ISR, CHANGE);
  // Set external 32k oscillator to run when idle or sleep mode is chosen
  SYSCTRL->XOSC32K.reg |=  (SYSCTRL_XOSC32K_RUNSTDBY | SYSCTRL_XOSC32K_ONDEMAND);
  REG_GCLK_CLKCTRL  |= GCLK_CLKCTRL_ID(GCM_EIC) | // generic clock multiplexer id for the external interrupt controller
                       GCLK_CLKCTRL_GEN_GCLK1 |   // generic clock 1 which is xosc32k
                       GCLK_CLKCTRL_CLKEN;        // enable it
  // Write protected, wait for sync
  while (GCLK->STATUS.bit.SYNCBUSY);

  // Set External Interrupt Controller to use channel 4
  EIC->WAKEUP.reg |= EIC_WAKEUP_WAKEUPEN4;

  PM->SLEEP.reg |= PM_SLEEP_IDLE_CPU;  // Enable Idle0 mode - sleep CPU clock only
  //PM->SLEEP.reg |= PM_SLEEP_IDLE_AHB; // Idle1 - sleep CPU and AHB clocks
  //PM->SLEEP.reg |= PM_SLEEP_IDLE_APB; // Idle2 - sleep CPU, AHB, and APB clocks

  // It is either Idle mode or Standby mode, not both.
  SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;   // Enable Standby or "deep sleep" mode

  // Built-in LED set to output and high
  PORT->Group[g_APinDescription[LED_BUILTIN].ulPort].DIRSET.reg = (uint32_t)(1<<g_APinDescription[LED_BUILTIN].ulPin);
  PORT->Group[g_APinDescription[LED_BUILTIN].ulPort].OUTSET.reg = (uint32_t)(1<<g_APinDescription[LED_BUILTIN].ulPin);
}

void rfCheck() {
  // Read from RF device
  while (Serial1.available() > 0) {
    msg = Serial1.readString();
    Serial.println((String)"RF: "+msg);
    // TODO do something when message is received
  }
}

void gpsCheck() {
  relay.gps.readGPSSerial(); // we need to keep reading in main loop to keep GPS serial buffer clear
  if (nowTimeDiff(gpsTimer) > GPS_WAKEUP_INTERVAL) {
    relay.gps.gpsWakeup(); // wake up the GPS until we get a fix or timeout
    // TODO: double check this timeout logic
    if (relay.gps.gpsHasFix() || (nowTimeDiff(gpsTimer) > GPS_WAKEUP_INTERVAL+GPS_LOCK_TIMEOUT)) { 
      relay.gps.gpsStandby();
      gpsTimer = millis(); // reset the timer
      #if DEBUG_MODE
      Serial.print("DEBUG: ");if (relay.gps.gpsHasFix()) {Serial.println("GOT GPS FIX");} else {Serial.println("GPS FIX TIMEOUT");}
      #endif
    }
  }
}

void batteryCheck() {
  if (nowTimeDiff(batteryCheckTimer) > BATTERY_CHECK_INTERVAL) {
    batteryCheckTimer = millis(); // reset the timer
    relay.checkBatteryVoltage();
  }
}

void sleepCheck() {
  if (nowTimeDiff(awakeTimer) > AWAKE_INTERVAL) {
    // set pin mode to low
    PORT->Group[g_APinDescription[LED_BUILTIN].ulPort].OUTCLR.reg = (uint32_t)(1<<g_APinDescription[LED_BUILTIN].ulPin);
    Serial.println("sleeping as timed out");
    __WFI();  // wake from interrupt
    Serial.println("wake due to interrupt");
    Serial.println();
  }
  // toggle output of built-in LED pin
  PORT->Group[g_APinDescription[LED_BUILTIN].ulPort].OUTTGL.reg = (uint32_t)(1<<g_APinDescription[LED_BUILTIN].ulPin);
}

void EIC_ISR(void) {
  awakeTimer = millis(); // refresh awake timer.
}
