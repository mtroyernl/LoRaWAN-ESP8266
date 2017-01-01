/*
 *  The Things Network LoRaWAN test object built for under $15.  Takes temperature, humidity, and 
 *  light sensor data and sends to the Things Network (LoRaWAN).
 *  
 *  This particular version was built to work with ABP using a single channel (868.1 mHZ) for use 
 *  with a single channel gateway for demonstrations where TTN network access may not be publically available.  
 *  8 channel functionality can be enabled in code.
 *  
 *  This was insipred from code written by Thomas Telkamp and Matthijs Kooijman  https://github.com/matthijskooijman/arduino-lmic/
 */

#include <lmic.h>           // Download from https://github.com/matthijskooijman/arduino-lmic/
#include <hal/hal.h>        // Download from https://github.com/matthijskooijman/arduino-lmic/
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <DHT.h>

// ESP8266 PIN USAGE
#define LED	D0 // GPIO 16 
#define DIO0	D1 // GPIO 05
#define DIO1	D2 // GPIO 04
#define NC	D3 // GPIO 00 Reserved - Pull high to boot from SPI Flash, low to program via UART
#define DHTPIN D4 // GPIO 02 Reserved - Pulled high.   Matches as input for DHT22
#define SCK   D5 // GPIO 14
#define	MISO	D6 // GPIO 12
#define MOSI	D7 // GPIO 13
#define NSS   D8 // GPIO 15 Reserved - Pulled low to boot from SPI Flash or UART

#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266
float humidity, temperature;
int light, motion;

// LoRaWAN, DevAddr, NwkSKey, AppSKey  - Use ABP Authentication to define the values required for operation.
// See http://thethingsnetwork.org/wiki/AddressSpace
// DevAddr - Logical address created during a session.  This is not the DEV EUI which is normally a MAC layer unique address.  
//           Many LoRa radio chip implementations (e.g. RFM9X) do not have a fixed DEV EUI.
// NwkSKey - Unique shared key created by the APB registration process for the notion of a session.   This is a static key using ABP.
// AppSKey - Unique shared key created by the APB registration process for a given application.   This is a static key using ABP.

static const u4_t DEVADDR = 0x026011361;
static const PROGMEM u1_t NWKSKEY[16] = { 0x8E, 0xB0, 0x83, 0x64, 0x29, 0xBF, 0xEE, 0xFB, 0xBB, 0x3F, 0xF2, 0xBD, 0x25, 0x9F, 0xE6, 0x55 };
static const u1_t PROGMEM APPSKEY[16] = { 0x02, 0x5E, 0xA2, 0x8A, 0xBD, 0xC8, 0x45, 0xA1, 0x3D, 0x20, 0x20, 0x1E, 0x0F, 0x7F, 0xD5, 0x47 };

// These callbacks are only used in over-the-air activation, so they are
// left empty here (we cannot leave them out completely unless
// DISABLE_JOIN is set in config.h, otherwise the linker will complain).
void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

static osjob_t sendjob;

// Schedule TX every this many seconds (might become longer due to duty
// cycle limitations).
const unsigned TX_INTERVAL = 60;

// These PIN assignments need to be adjusted for available pins on the ESP8266.  Using D8, D1, D2 works fine.
const lmic_pinmap lmic_pins = {
    .nss = 15, // D8
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LMIC_UNUSED_PIN,
    .dio = {4, 5, LMIC_UNUSED_PIN},  // D1, D2
};

void blinkn (int i, int pin ) {
  int j;
  for (j=0; j < i; j++) {
    digitalWrite(pin, HIGH);
    delay (200);
    digitalWrite(pin, LOW);
    delay (200); 
  } 
}

void getReadings() {
    humidity = dht.readHumidity();
    yield();
    temperature = dht.readTemperature();
    yield();
    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("Failed to read from DHT sensor!");
      humidity = 0;
      temperature = 0;
    }
    yield();
    light = analogRead(A0);
    yield();
}

void do_send(osjob_t* j);
void onEvent (ev_t ev) {
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
            break;
        case EV_TXCOMPLETE:
            blinkn (1,LED); // Blink the LED once to indicate that this callback was received.
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK)
              Serial.println(F("Received ack"));
            if (LMIC.dataLen) {
              blinkn (LMIC.dataLen,LED); // Blink the LED to show byte size of data received.
              Serial.print(F("Received "));
              Serial.print(LMIC.dataLen);
              Serial.println(F(" bytes of payload"));
            }
            // Schedule next transmission
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(TX_INTERVAL), do_send);
            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            break;
    }
}

void do_send(osjob_t* j){
    blinkn (2,LED); // Blink the LED twice to indicate that we will be sending data shortly
    getReadings();

    int16_t temperatureb2 = (int16_t)(temperature * 100);
    int16_t humidityb2 = (int16_t)(humidity * 100);
    int16_t lightb2 = (int16_t)(light * 100);
    
    byte data[7];
    data[0] = temperatureb2 >> 8;
    data[1] = temperatureb2 & 0xFF;
    data[2] = humidityb2 >> 8;
    data[3] = humidityb2 & 0xFF;
    data[4] = lightb2 >> 8;
    data[5] = lightb2 & 0xFF;
    
    // Check if there is not a current TX/RX job running
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND, not sending"));
    } else {
        // Prepare upstream data transmission at the next possible time.
        LMIC_setTxData2(1, data, sizeof(data)-1, 0);
        Serial.println(F("Packet queued"));
    }
    // Next TX is scheduled after TX_COMPLETE event.
}

void setup() {
    WiFi.forceSleepBegin(); // turn off ESP8266 RF
    delay(1);               // give RF section time to shutdown
  
    Serial.begin(115200);
    Serial.println(F("Starting"));

    // Start up DHT sensor, wait and take an initial reading
    dht.begin();            
    delay (200);
    getReadings();
    
    pinMode(LED, OUTPUT);
    
    #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
    #endif

    // LMIC init
    os_init();
    yield();
    // Reset the MAC state. Session and pending data transfers will be discarded.
    LMIC_reset();
    yield();

    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    #ifdef PROGMEM
    // On AVR, these values are stored in flash and only copied to RAM
    // once. Copy them to a temporary buffer here, LMIC_setSession will
    // copy them into a buffer of its own again.
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);
    #else
    // If not running an AVR with PROGMEM, just use the arrays directly 
    LMIC_setSession (0x1, DEVADDR, NWKSKEY, APPSKEY);
    #endif

    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set.

    //Limit this to 868100000 for single channel use

    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
 
    // Use with multiple channels
    /*
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI);      // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);      // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK,  DR_FSK),  BAND_MILLI);     // g2-band
 */
    // TTN defines an additional channel at 869.525Mhz using SF9 for class B
    // devices' ping slots. LMIC does not have an easy way to define set this
    // frequency and support for class B is spotty and untested, so this
    // frequency is not configured here.

    // Disable link check validation
    LMIC_setLinkCheckMode(0);

    // Set data rate and transmit power (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7,14);

    // Start job
    do_send(&sendjob);
}

void loop() {
    os_runloop_once();
}




