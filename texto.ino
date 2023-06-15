#include "textolib.h"
#include "AnalogQWERTY.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define LENGTH(in) (sizeof(in) / sizeof(in[0]))

#define STM32 // board def (there has to be a better way, right Arduino IDE?)
#define DISPLAY_OLED_SSD1306_I2C
//#define DISPLAY_LCD_SPI

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#ifdef STM32
#define NUM_KEYPADS 3
int KEYBOARD_PINS[] = { PA0, PA1, PA2 };
#define POWERKEY PA15
#define LEDPIN PC13
#define BTN_LEFT PB12
#define BTN_OK PB13
#define BTN_RIGHT PB14
#endif

#ifdef ATMEGA328
#define NUM_KEYPADS 3
int KEYBOARD_PINS[] = { A0, A1, A2 };
#define POWERKEY 2
#define LEDPIN 13
#define BTN_LEFT ?
#define BTN_OK ?
#define BTN_RIGHT ?
#endif

#define DPAD_LEFT 4
#define DPAD_OK 2
#define DPAD_RIGHT 1

#define ENTRY_BUFFER 256
#define RESPONSE_BUFFER 2048 
#define RECORD_BUFFER (75 + 256) * 20 // CMGL header space + message space for a single record
// +CMGL: ###,"REC UNREAD","+997777777777","$$$$$","23/05/15,13:17:44-16".$MSG

#define STRING_RECIPIENT  "Recipient"
#define STRING_MESSAGE    "Message"

AnalogQWERTY keyboard;
String command;

bool refresh, entryMode;

enum {
  MENUITEM_MESSAGES = 0,
  MENUITEM_CONTACTS,
  MENUITEM_SETTINGS,
  MENUITEM_MESSAGE_NEW,
  MENUITEM_NEWMSG_RECIPIENT,
  MENUITEM_NEWMSG_BODY,
  MENUITEM_NEWMSG_SEND,
  MENUITEM_BACK
};

enum {
  ACTION_LOAD_MESSAGES = 0,
  ACTION_NEW_MESSAGE,
  ACTION_BACK,
  ACTION_ENTRY,
  ACTION_SEND
};

MenuItem MENU_MAIN[] = {
  { .label = "Messages", MENUITEM_MESSAGES, ACTION_LOAD_MESSAGES, 2 },
  { .label = "Contacts", MENUITEM_CONTACTS, 0, 2 },
  { .label = "Settings", MENUITEM_SETTINGS, 0, 2 }
};

MenuItem MENU_MESSAGES[] = {
  { .label = "Back", MENUITEM_BACK, ACTION_BACK, 2},
  { .label = "New SMS", MENUITEM_MESSAGE_NEW, ACTION_NEW_MESSAGE, 2}
};

MenuItem MENU_NEWMESSAGE[] = {
  { .label = NULL, MENUITEM_NEWMSG_RECIPIENT, ACTION_ENTRY, 1},
  { .label = NULL, MENUITEM_NEWMSG_BODY, ACTION_ENTRY, 1},
  { .label = "Cancel", MENUITEM_BACK, ACTION_BACK, 2},
  { .label = "Send", MENUITEM_NEWMSG_SEND, ACTION_SEND, 2}
};

MenuItem * menu;
int menu_index, menu_max;
char entry[ENTRY_BUFFER]; // SMS supposably has a 160 character limit anyway
char response[RESPONSE_BUFFER];
int pos; // cursor position

void setup() {

  // general setup
  Serial.begin(9600);
  pinMode(LEDPIN, OUTPUT); // LED connect to pin PC13

  // dpad init
  pinMode(BTN_LEFT, INPUT);
  pinMode(BTN_OK, INPUT);
  pinMode(BTN_RIGHT, INPUT);

  // variable initialization
  command = String("");
  strcpy(response, "error");
  refresh = true; 
  entryMode = false;
  pos = 0;

  // input initialization
  // new message inputs
  MENU_NEWMESSAGE[0].label = (char*)malloc(ENTRY_BUFFER * sizeof(char));
  strcpy(MENU_NEWMESSAGE[0].label, STRING_RECIPIENT);

  MENU_NEWMESSAGE[1].label = (char*)malloc(ENTRY_BUFFER * sizeof(char));
  strcpy(MENU_NEWMESSAGE[1].label, STRING_MESSAGE);

  // display init
   // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  splash();

  // keyboard init
  keyboard = AnalogQWERTY();
  keyboard.init(KEYBOARD_PINS, NUM_KEYPADS);
  
  // LTE module init
  texto.PowerOn(POWERKEY);
  texto.Initialize(5000);

  loadMenu(MENU_MAIN, LENGTH(MENU_MAIN));

  texto.sendATcommand("AT+CMGF=1", "OK", 10000);

  Serial.println("initialized");
  delay(500);
  Serial.println("loading messages");
  loadMessages();
}

void loadMenu(MenuItem* newmenu, int num_items) {
  menu = newmenu;
  menu_index = 0;
  menu_max = num_items;
  refresh = true;
}

int getMessageCount() {
  texto.getATcommandResponse("AT+CPMS?", "CPMS+", response, RESPONSE_BUFFER, 3000);
  // +CPMS: "ME",16,255,"ME",16,255,"ME",16,255
  char* index = strstr(response, "\",");
  int count = 0;
  if(index != NULL) {
    char ascii[3];
    int i = 0;
    index+=2;
    do {
      ascii[i++] = *(index++);
    }while(*index != ',');
    count = atoi(ascii);
  }
  return count;
}

void loadMessages() {
  
  // extract message count from AT+CPMS? response
  int count = getMessageCount();
  delay(500);

  Message ** messagebuffer;
  if(texto.getMessages("ALL", "+CMGL", messagebuffer, count, 10000) == 0) {
    strcpy(response, "load message failure");
  } else {
    strcpy(response, "load message success");
  }
}
void loop() {
  blink();

  // process keys
  int dpad = (digitalRead(BTN_LEFT) << 2) | (digitalRead(BTN_OK) << 1) | digitalRead(BTN_RIGHT);
  char key = keyboard.getKeyPress();

  if(dpad > 0) {
    if(entryMode) {
      strcpy(menu[menu_index].label, entry);
      entryMode = false;
    }
    handleDPad(dpad);
  }

  if(entryMode) {
    if(key == RETURN_KEY) {
      entry[pos++] = '\n';
    } else if (key == DELETE_KEY) {
      if(pos > 0) {
        pos--;
      }
      entry[pos] = '\0';
    } else if (key != NULL) {
      entry[pos++] = key;
    }
    refresh = true;
  }

  paint();  
}

void handleDPad(int dpad_flags) {
  refresh = true;
  if((dpad_flags & DPAD_LEFT) != 0) {
    menu_index = menu_index-1 >= 0 ? menu_index-1 : menu_max-1;
  }
  if((dpad_flags & DPAD_RIGHT) != 0) {
    menu_index = menu_index+1 < menu_max ? menu_index+1 : 0;
  }
  if((dpad_flags & DPAD_OK) != 0) {
    switch(menu[menu_index].action) {
      case ACTION_LOAD_MESSAGES:
      {
        loadMenu(MENU_MESSAGES, LENGTH(MENU_MESSAGES));
        break;
      }
      case ACTION_NEW_MESSAGE:
      {
        loadMenu(MENU_NEWMESSAGE, LENGTH(MENU_NEWMESSAGE));
        break;
      }
      case ACTION_BACK:
      {
        loadMenu(MENU_MAIN, LENGTH(MENU_MAIN));
        break;
      }
      case ACTION_SEND:
      {
        // TODO validate inputs
        char * phonenumber = menu[0].label;
        char * message = menu[1].label;
        // call texto
        texto.SendingShortMessage(phonenumber, message);
        break;
      }
      case ACTION_ENTRY:
      {
        // clear field for entry
        memset(entry, '\0', LENGTH(entry));
        //entry[0] = '\0';
        entryMode = true;
        pos = 0;
        break;
      }
      default:
        break;
    }
  }
}

void paint() {
  if(refresh) {
    // Clear the buffer
    display.clearDisplay();
    display.setCursor(0, 0);

    for(int i = 0; i < menu_max; i++) {
      char * temp = (!entryMode || i != menu_index) ? menu[i].label : &entry[0];
      display.setTextSize(menu[i].size);
      if(i == menu_index) {
        // highlight selection
        int16_t x,y;
        uint16_t w,h;
        display.getTextBounds(temp, 0, 0, &x, &y, &w, &h);
        display.fillRect(display.getCursorX(), display.getCursorY(), w, h, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      display.println(temp);
    }
    display.display();
    refresh = false;
  }
}

void blink() {
  digitalWrite(PC13, HIGH);   // turn the LED on (HIGH is the voltage level)
  delay(100);               // wait for 100mS
  digitalWrite(PC13, LOW);    // turn the LED off by making the voltage LOW
  delay(100);
}

void splash()
{
  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println(F("Texto v0"));
  // Show the display buffer on the screen. You MUST call display() after
  // drawing commands to make them visible on screen!
  display.display();
  delay(2000);
}