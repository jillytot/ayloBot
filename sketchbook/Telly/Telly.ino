// vim:set sw=4 ts=4 ai et:

/*
 * Arduino sketch for Telly
 * (c) Copyright 2015 Let's Robot.
 *
 * This sketch is connected via serial to a Raspberry Pi, which sends one
 * character movement commands to move the robot in discrete chunks: 'f' for
 * forward, 'b' for backward, 'l' for left, 'r' for right.  The program on the
 * Pi is very limited, and will only accept output of "ok\n" to indicate a
 * command is done executing.  No other output may appear.  That program will
 * be updated in the future to be more robust.  The command set will be updated
 * then, too.
 *
 * The Pi also acts as an I2C master, sending LED color information.  The
 * protocol is just four bytes: the LED index number, and the red, green, and
 * blue values.  If the LED number is 0xFF, all of the LEDs are set.  When the
 * program on the Pi is updated, LED commands will come over the serial port,
 * too, and nothing will be speaking I2C.
 */

#include <Servo.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

#define OK_STRING   "ok\n"

#define TEENSY  // Are we running on a teensy, or an Uno?

#ifdef TEENSY
    #define LED_PIN         3
    #define LEFT_PIN        8
    #define RIGHT_PIN       10
#else
    #define LED_PIN         6
    #define LEFT_PIN        9
    #define RIGHT_PIN       10
#endif


/*
 * Continuous rotation servos for the two wheels.  Because of the way the
 * servos had to be mounted, they're facing different directions.  With this
 * library, 90 is the stop value, 0 is full-speed in one direction, and 180
 * is full-speed in the other direction.
 */
#define LEFT_STOP       95
#define LEFT_FORWARD    (LEFT_STOP  -10)
#define LEFT_BACKWARD   (LEFT_STOP  +10)
//
#define RIGHT_STOP      95
#define RIGHT_FORWARD   (RIGHT_STOP +10)
#define RIGHT_BACKWARD  (RIGHT_STOP -10)

Servo left_servo, right_servo;

// How long in milliseconds to drive the motors when moving forward and back
#define DRIVE_TIME 1000

// How long in milliseconds to drive the motors when moving left and right
#define TURN_TIME 250

// LED NeoPixel strip for the eyes, driven by the Raspberry Pi speaking I2C
#define I2C_ADDRESS      0x04
#define NUM_LEDS         (9*2)
#define I2C_TIMEOUT_TIME 1000

Adafruit_NeoPixel eyes = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

int eye_state;

/*
 * The mapping of LEDs as they appear on the NeoPixel strip, vs the human
 * address we'd like to give them.
 */
int led_map[NUM_LEDS] = {
         0,  //  0
         3,  //  1
         6,  //  2
         1,  //  3
         4,  //  4
         7,  //  5
         2,  //  6
         5,  //  7
         8,  //  8
        15,  //  9
        16,  // 10
        17,  // 11
        12,  // 12
        13,  // 13
        14,  // 14
         9,  // 15
        10,  // 16
        11,  // 17
    };

void setup() {
    Serial.begin(9600);

    left_servo.attach(LEFT_PIN);
    right_servo.attach(RIGHT_PIN);

    eyes.begin();
    eyes.show();

    /*
     * Become an I2C slave.  First change the bitrate to 400khz, which must be
     * set before calling Wire.begin().  Then set the I2C callback functions.
     */
    TWBR = 12;
    Wire.begin(I2C_ADDRESS);
    Wire.onReceive(receiveData);
    Wire.onRequest(sendData);

    pinMode(LED_BUILTIN, OUTPUT);

    for (int i = 0; i < 8; i++) {
        digitalWrite(LED_BUILTIN, LOW);
        delay(33);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(33);
    }
}

#ifdef TEENSY
/*
 * Teensy reset code from https://www.pjrc.com/teensy/jump_to_bootloader.html
 */
void reset(void) {
    volatile static int barrier;
    Serial.println("Resetting");
    delay(100);
    barrier++;

    cli();
    UDCON = 1;
    USBCON = (1<<FRZCLK);
    UCSR1B = 0;
    delay(100);
    EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
    TIMSK0 = 0; TIMSK1 = 0; TIMSK3 = 0; TIMSK4 = 0; UCSR1B = 0; TWCR = 0;
    DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0; TWCR = 0;
    PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
    asm volatile("jmp 0x7E00");

    Serial.println("Did the reset fail?");
}
#endif

void OK() {
    Serial.print(OK_STRING);
}

void move(int left, int right) {
    left_servo.write(left);
    right_servo.write(right);
}

void stop(void) {
    left_servo.write(LEFT_STOP);
    right_servo.write(RIGHT_STOP);
    OK();
}

void loop() {
    char c;

    while (Serial.available()) {
        c = Serial.read();
        switch (c) {
                case 'f': move(LEFT_FORWARD,  RIGHT_FORWARD);  delay(DRIVE_TIME); stop(); break;
                case 'b': move(LEFT_BACKWARD, RIGHT_BACKWARD); delay(DRIVE_TIME); stop(); break;
                case 'l': move(LEFT_BACKWARD, RIGHT_FORWARD);  delay(TURN_TIME);  stop(); break;
                case 'r': move(LEFT_FORWARD,  RIGHT_BACKWARD); delay(TURN_TIME);  stop(); break;

                case 'X': eye_state = 0; break;

                #ifdef TEENSY
                case '#': reset(); break;
                #endif
        }
    }
}

/*
 * I2C callbacks
 */

void set_color(int pixel, int R, int G, int B) {
    int i;

    if (pixel == 0xFF) {
        for (i = 0; i < NUM_LEDS; i++)
            eyes.setPixelColor(i, R, G, B);
        eyes.show();
        return;
    }

    /*
     * The human-readable pixel index is 1 based, so the valid range
     * is [1, NUM_LEDS], not [0, NUM_LEDS-1].  After input validation,
     * subtract one from index to use a stanard 0-based array.
     */
    if (! (1 <= pixel && pixel <= NUM_LEDS))
        return;

    pixel -= 1;
    eyes.setPixelColor(led_map[pixel], R, G, B);
    eyes.show();
}

void sendData() {
    Wire.write(eye_state);
}

void receiveData(int num_bytes) {
    static int R, G, B;
    static int pixel;
    static unsigned long last_read;

    /*
     * A timeout event, to help avoid out-of-sync errors with the Pi.  If we
     * haven't heard anything from the Pi in over half a second, go back to
     * state 0, assuming the next byte we're gonig to read is the start of a
     * new LED command.
     */
    if (millis() - last_read > I2C_TIMEOUT_TIME)
        eye_state = 0;

    last_read = millis();

    while (Wire.available()) {
        uint8_t val = Wire.read();

        switch (eye_state) {
            case 0:
                pixel = val;
                eye_state++;
                break;

            case 1:
                R = val;
                eye_state++;
                break;

            case 2:
                G = val;
                eye_state++;
                break;

            case 3:
                B = val;
                eye_state = 0;
                set_color(pixel, R, G, B);
                break;
        }
    }
}
