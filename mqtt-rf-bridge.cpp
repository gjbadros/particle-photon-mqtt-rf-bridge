// See also bin/share/decode_rf433mhz_clock_temp_signal.pl

#include "application.h"
#include "mqtt.h"

// edit mqtt-credentials.h.example to create this file
// (but do not commit your real credentials)
#include "mqtt-credentials.h"

// Example of publishing BGL to the projection clock
// mosquitto_pub -h [host] -u [username] -P [password] -t photon/http_rf_bridge_temp -m 75

void callback(char* topic, byte* payload, unsigned int length);

/**
 * if want to use IP address,
 * byte server[] = { XXX,XXX,XXX,XXX };
 * MQTT client(server, 1883, callback);
 * want to use domain name,
 * exp) iot.eclipse.org is Eclipse Open MQTT Broker: https://iot.eclipse.org/getting-started
 * MQTT client("iot.eclipse.org", 1883, callback);
 **/
MQTT client(MQTT_HOST, MQTT_PORT, callback);

int transmit = D7;

// most recent mqtt-received message, fully encoded and ready to be sent
char encoded_msg[64 * 5 + 2];
const int ms_frequency = 57000; // 57 seconds
long last_ms_sent = 0;
bool fClockInFahrenheitMode = true;
// whether to publish particle events for debugging/testing;
// turn off to eliminate those events
bool fEventDebug = true;
// whether to debug via serial messages
bool fSerialDebug = false;

// 24.9 degC = 00000101100000001111100111111001101110

// msg is ascii string of '0' and '1' characters
// timing is 500 us
void send_message(int xmit_pin, const char *msg) {
    SINGLE_THREADED_BLOCK() {
        for (int i = 0; i<8; ++i) {
            for (const char *pch = msg; *pch; ++pch) {
                digitalWrite(xmit_pin, (*pch == '0'? LOW: HIGH));
                delayMicroseconds(500);
            }
            digitalWrite(xmit_pin, LOW);
            delayMicroseconds(500);
            digitalWrite(xmit_pin, HIGH);
            delayMicroseconds(500);
            digitalWrite(xmit_pin, LOW);
            delayMicroseconds(4000);
        }
        digitalWrite(xmit_pin, LOW);
        last_ms_sent = millis();
    }
}

// recieve MQTT message, payload is one of:
// debug_serial_on
// debug_serial_off
// debug_event_on
// debug_event_off
// degF
// degC
// [number]   # the temp
// [number],[number] # the temp and the "yyyy.." suffix
void callback(char* topic, byte* payload, unsigned int length) {
    char p[length + 1];
    memcpy(p, payload, length);
    p[length] = '\0';
    if (0 == strcmp(p, "debug_serial_on")) {
        fSerialDebug = true; return;
    } else if (0 == strcmp(p, "debug_serial_off")) {
        fSerialDebug = false; return;
    } else if (0 == strcmp(p, "debug_event_on")) {
        fEventDebug = true; return;
    } else if (0 == strcmp(p, "debug_event_off")) {
        fEventDebug = false; return;
    } else if (0 == strcmp(p, "degF")) {
        fClockInFahrenheitMode = true; return;
    } else if (0 == strcmp(p, "degC")) {
        fClockInFahrenheitMode = false; return;
    }

    char *pchBGL = p;
    char *pchChecksum = strchr(p, ',');
    if (pchChecksum) {
        *pchChecksum++ = '\0';
    }

    // TOOO: I still don't understand these yyyyyyyy bits after
    // the binary-encoded temperature.
//  char full_msg[] = "000001011000xxxxxxxxxxxx111110011011";
//  char full_msg[] = "000001011000xxxxxxxxxxxx111101101001"; // for 19.6
//  char full_msg[] = "000001011000xxxxxxxxxxxx111101000010"; // for 25.1
    char full_msg[] = "000001011000xxxxxxxxxxxx1111yyyyyyyy"; // for 25.1
    int ichTempBinary = 12;
    int cchTempBinary = 12;
    int ichChecksum = ichTempBinary + cchTempBinary + 4;
    int cchChecksum = 8;
    int cchFullMsg = ichChecksum + cchChecksum;

    int bgl = atoi(pchBGL);
    int temp_to_send = bgl; // for centigrade mode
    if (fClockInFahrenheitMode) {
        // if clock is in fahrenheit, we need to treat bgl as a degF
        // temperature and convert it to centigrade before sending
        temp_to_send = int(10*5.0*(bgl/10.0-32.0)/9.0 - 0.5);
    }
    int checksum = 0;
    if (pchChecksum) {
        checksum = atoi(pchChecksum);
    }
    int temp = temp_to_send;
    for (int ich = ichTempBinary + cchTempBinary - 1; ich >= ichTempBinary; --ich) {
        full_msg[ich] = temp % 2? '1': '0';
        temp >>= 1;
    }
    for (int ich = ichChecksum + cchChecksum - 1; ich >= ichChecksum; --ich) {
        full_msg[ich] = checksum % 2? '1': '0';
       checksum >>= 1;
    }

    // encoded_msg uses:
    // 0 -> 00
    // 1 -> 0000
    // with ones separating each binary digit; e.g.,
    // 00110 -> 10010010000100001001
    char *pchEncoded = encoded_msg;
    *pchEncoded++ = '1';
    for (char *pch = full_msg; *pch; ++pch) {
        *pchEncoded++ = '0';
        *pchEncoded++ = '0';
        if (*pch == '1') {
            *pchEncoded++ = '0';
            *pchEncoded++ = '0';
        }
        *pchEncoded++ = '1';
    }
    *pchEncoded = '\0';

    if (fSerialDebug) {
        Serial.println(pchBGL);
        if (pchChecksum) {
            Serial.println(pchChecksum);
        }
        Serial.println(full_msg);
        Serial.println(encoded_msg);
    }
    if (fEventDebug) {
        Particle.publish("p", p, PRIVATE);
        char buf[8]; sprintf(buf, "%4d", temp_to_send);
        Particle.publish("temp", buf, PRIVATE);
        Particle.publish("full_msg", full_msg, PRIVATE);
        Particle.publish("encoded_msg", encoded_msg, PRIVATE);
    }
}

void connect() {
    // connect to the server
    // make sure you copy mqtt-credentials.h.example to
    // mqtt-credentials.h and update the two #defines there
    client.connect("http_rf_bridge_" + String(Time.now()),
                   MQTT_USERNAME, MQTT_PASSWORD,
                   MQTT_TOPIC, MQTT::QOS2, false,
                   "terminated", true);
    
    // publish/subscribe
    if (client.isConnected()) {
        client.publish(MQTT_TOPIC, "listening");
        client.subscribe(MQTT_TOPIC_TEMP);
    }
}

void setup() {
    pinMode(transmit, OUTPUT);
    digitalWrite(transmit, LOW);

    // mqtt connection
    connect();

    // we initialize this here so that we send our first
    // RF signal 57 seconds after setup(); we send "333"
    // this ensures that we catch the projection clock
    // in its first 2-3 minute sync period after long-pressing
    // "+" button
    last_ms_sent = millis();
    callback(MQTT_TOPIC_TEMP, (byte *) "333", 3);

    Serial.begin(9600);   // Debugging only
}

void loop() {
    if (client.isConnected()) {
        client.loop();
        if ((millis() - last_ms_sent) > ms_frequency) {
            send_message(transmit, encoded_msg);
            if (fEventDebug) {
                Particle.publish("sent", "", PRIVATE);
            }
        }
        delay(200);
    } else {
        delay(1000);
        connect();
    }
}
