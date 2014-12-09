#include <DeviceHive.h>

#define CMD_STATUS_SUCCESS      "Success"
#define CMD_STATUS_FAILED       "Failed"
#define CMD_RESULT_OK           "OK"

// device registration data
// intent numbers should be greater than 255!
// please refer to http://www.devicehive.com/binary/#SystemMessages/RegisterJson for complete syntax of registration info
const char *REG_DATA = "{"
    "id:'f4b07b4d-5b02-47e7-bd9b-c8d56e0cfdd1',"
    "key:'Arduino_LED',"
    "name:'Arduino LED',"
    "deviceClass:{"
        "name:'Arduino_LED',"
        "version:'1.0'},"
    "equipment:[{code:'led',name:'led',type:'led'}],"
    "commands:["
        "{intent:1000,name:'set',params:u8},"
        "{intent:1001,name:'blink',params:{on:u16,off:u16,count:u8}},"
        "{intent:1002,name:'text',params:s},"
        "{intent:1003,name:'doubletext',params:{s0:s,s1:s}}"
    "],"
    "notifications:["
        "{intent:2000,name:'button',params:u8}"
    "]"
"}";

const int BTN_PIN = 12;
const int LED_PIN = 13;

// VERY IMPORTANT: the order and types of fields in struct
// should be exactly the same as those defined in registration data
// {\"intent\":1001,\"name\":\"blink\",\"params\":{\"on\":\"u16\",\"off\":\"u16\",\"count\":\"u8\"}}

struct BlinkParam
{
    unsigned short on;
    unsigned short off;
    byte count;
};

void setLedState(int state)
{
    digitalWrite(LED_PIN, state ? HIGH : LOW);
}

void sendButtonState(int state)
{
    OutputMessage tx_msg(2000);
    tx_msg.putByte(state);
    DH.write(tx_msg);
}

int lcd_getKey(void);

int old_btn_state;

#include <LiquidCrystal.h>
static LiquidCrystal lcd(8, 9, 4, 5, 6, 7); // 4-wire mode

enum LcdKeys
{
    KEY_NONE    = 0x00,
    KEY_RIGHT   = 0x01,
    KEY_UP      = 0x02,
    KEY_DOWN    = 0x04,
    KEY_LEFT    = 0x08,
    KEY_SELECT  = 0x10
};

void sendRegistrationResponse(InputMessageEx& rx_msg, const long cmd_id)
{
    DH.writeRegistrationResponse(REG_DATA);
}

void processSetMsg(InputMessageEx& rx_msg, const long cmd_id)
{
    const byte state = rx_msg.getByte();
    setLedState(state);
    DH.writeCommandResult(cmd_id, CMD_STATUS_SUCCESS, CMD_RESULT_OK);
}

void processBlinkMsg(InputMessageEx& rx_msg, const long cmd_id)
{
    BlinkParam params = rx_msg.get<BlinkParam>();
    // TODO: check for very long delays?

    for (int i = 0; i < params.count; ++i)
    {
        setLedState(1);     // ON
        delay(params.on);
        setLedState(0);     // OFF
        delay(params.off);
    }
    DH.writeCommandResult(cmd_id, CMD_STATUS_SUCCESS, CMD_RESULT_OK);
}

void processTextMsg(InputMessageEx& rx_msg, const long cmd_id)
{
    unsigned int maxLen = rx_msg.length;
    maxLen -= sizeof(long);
    char msg[maxLen];
    rx_msg.getString(msg, maxLen);
    lcd.clear();
    lcd.print(msg);
    DH.writeCommandResult(cmd_id, CMD_STATUS_SUCCESS, CMD_RESULT_OK);
}

void processDoubletextMsg(InputMessageEx& rx_msg, const long cmd_id)
{
    char buf[64];

    // print first line
    rx_msg.getString(buf, sizeof(buf));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(buf);

    // print second line
    rx_msg.getString(buf, sizeof(buf));
    lcd.setCursor(0, 1);
    lcd.print(buf);

    DH.writeCommandResult(cmd_id, CMD_STATUS_SUCCESS, CMD_RESULT_OK);
}

/**
 * Initializes the Arduino firmware.
 */
void setup(void)
{
    lcd.begin(16, 2);       // start the library

    pinMode(LED_PIN, OUTPUT);
    pinMode(BTN_PIN, INPUT_PULLUP); // ... so you don't need a pull-up resistor
    old_btn_state = digitalRead(BTN_PIN);

    Serial.begin(115200);
    DH.begin(Serial);
    DH.writeRegistrationResponse(REG_DATA);
    
    DH.registerCallback(INTENT_REGISTRATION_REQUEST, sendRegistrationResponse); // registration data needed
    DH.registerCallback(1000, processSetMsg); // "set" - sets the LED state
    DH.registerCallback(1001, processBlinkMsg); // "blink" - blinks the LED
    DH.registerCallback(1002, processTextMsg); // "text"
    DH.registerCallback(1003, processDoubletextMsg); // "doubletext"
    
    lcd.setCursor(0,0);
    lcd.print("Hello");
}

/**
 * Loop procedure is called continuously.
 */
void loop(void)
{
    // ned button state change notifications
    const int btn_state = lcd_getKey();
    if (btn_state != old_btn_state)
    {
        unsigned long now = millis();
        static unsigned long last = now;
        if ((now - last) > 500)
        {
            sendButtonState(btn_state);
            old_btn_state = btn_state;
            last = now;
        }
    }

    DH.process();
}

/**
 * @brief Get pressed key on LCD Keypad Shield.
 * @return The key mask.
 */
int lcd_getKey(void)
{
    const int key = analogRead(0); // analog pin #0

    if (key < 60)
        return KEY_RIGHT;
    else if (key < 200)
        return KEY_UP;
    else if (key < 400)
        return KEY_DOWN;
    else if (key < 600)
        return KEY_LEFT;
    else if (key < 800)
        return KEY_SELECT;

    return KEY_NONE;
}
