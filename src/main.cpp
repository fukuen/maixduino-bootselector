/*--------------------------------------------------------------------
Copyright 2020 fukuen

Boot selector is free software: you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This software is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with This software.  If not, see
<http://www.gnu.org/licenses/>.
--------------------------------------------------------------------*/

#include <Arduino.h>
#include <Sipeed_ST7789.h>
#include <stdio.h>
#include "fpioa.h"
#include "utility/Button.h"
#include "w25qxx.h"

SPIClass spi_(SPI0); // MUST be SPI0 for Maix series on board LCD
Sipeed_ST7789 lcd(320, 240, spi_);

#define DEBOUNCE_MS 10
#ifdef PIN_KEY_UP // Maix-go
Button BtnBOOT = Button(KEY0, true, DEBOUNCE_MS);
Button BtnA = Button(PIN_KEY_UP, true, DEBOUNCE_MS);
Button BtnB = Button(PIN_KEY_PRESS, true, DEBOUNCE_MS);
Button BtnC = Button(PIN_KEY_DOWN, true, DEBOUNCE_MS);
#else
Button BtnBOOT = Button(KEY0, true, DEBOUNCE_MS);
Button BtnA = Button(7, true, DEBOUNCE_MS);
Button BtnB = Button(8, true, DEBOUNCE_MS);
Button BtnC = Button(9, true, DEBOUNCE_MS);
#endif
#define BOOT_CONFIG_ADDR        0x00004000  // main boot config sector in flash at 52K
#define BOOT_CONFIG_ITEMS       8           // number of handled config entries

int posCursor = 0;
int posLaunch = 0;
bool btnLongPressed = false;
uint8_t buff[4096];

typedef struct {
  uint32_t entry_id;
  uint32_t app_start;
  uint32_t app_size;
  uint32_t app_crc;
  char app_name[16];
} app_entry_t;

app_entry_t app_entry[8];

void spcDump2(char *id, int rc, uint8_t *data, int len) {
    int i;
    printf("[%s] = %d\n",id,rc);
    for(i=0;i<len;i++) {
      printf("%0x ",data[i]);
      if ( (i % 10) == 9) printf("\n");
    }
    printf("\n");
}

/*
 * Get uint32_t value from Flash
 * 8-bit Flash pionter is used,
 */
//-----------------------------------------
static uint32_t flash2uint32(uint32_t addr) {
    uint32_t val = buff[addr] << 24;
    val += buff[addr+1] << 16;
    val += buff[addr+2] << 8;
    val += buff[addr+3];
    return val;
}

bool checkKboot() {
  w25qxx_init(3, 0);

  uint8_t manuf_id = 0;
  uint8_t device_id = 0;
  w25qxx_read_id(&manuf_id, &device_id);
  printf("id: %u %u\n", manuf_id, device_id);

  int rc = w25qxx_read_data(0x1000, buff, 0x20, W25QXX_STANDARD);
//  spcDump2("dump", rc, buff, 32);

  if (buff[0] == 0x00 && buff[9] == 0x4b && buff[10] == 0x4b
    && buff[11] == 0x62 && buff[12] == 0x6f && buff[13] == 0x6f
    && buff[14] == 0x74) {
    return true;
  }
  return false;
}

void readEntry() {
  int rc = w25qxx_read_data(BOOT_CONFIG_ADDR, buff, BOOT_CONFIG_ITEMS * 0x20, W25QXX_STANDARD);
//  spcDump2("dump", rc, buff, BOOT_CONFIG_ITEMS * 0x20);
  for (int i = 0; i < BOOT_CONFIG_ITEMS; i++) {
    app_entry[i].entry_id = flash2uint32(i * 0x20);
    app_entry[i].app_start = flash2uint32(i * 0x20 + 4);
    app_entry[i].app_size = flash2uint32(i * 0x20 + 8);
    app_entry[i].app_crc = flash2uint32(i * 0x20 + 12);
    for (int j = 0; j < 16; j++) {
      app_entry[i].app_name[j] = buff[i * 0x20 + 16 + j];
    }
  }
}

void writeActive(int index, int active) {
  int rc = w25qxx_read_data(BOOT_CONFIG_ADDR + index * 0x20 + 3, buff, 1, W25QXX_STANDARD);
  if (active == 0) {
    buff[0] = buff[0] & 0xfe;
  } else {
    buff[0] = buff[0] | 1;
  }
  rc = w25qxx_write_data(BOOT_CONFIG_ADDR + index * 0x20 + 3, buff, 1);
}

void drawMenu() {
  lcd.fillScreen(COLOR_BLACK);
  lcd.fillRect(0, 0, 320, 20, COLOR_CYAN);
  lcd.setCursor(1, 1);
  lcd.setTextSize(2);
  lcd.setTextColor(COLOR_BLACK);
  lcd.println("Boot Selector v0.1");
  lcd.setCursor(0, 16);
  lcd.println("");
  lcd.setTextColor(COLOR_WHITE);
  for (int i = 0; i < BOOT_CONFIG_ITEMS; i++) {
    lcd.print(" ");
    if ((app_entry[i].entry_id & 1) == 1) {
      lcd.setTextColor(COLOR_RED);
      lcd.print("*");
    } else {
      lcd.print(" ");
    }
    lcd.setTextColor(COLOR_WHITE);
    lcd.print(i + 1);
    lcd.print(".");
    lcd.print(app_entry[i].app_name);
    lcd.println("");
  }
  lcd.println("");
  lcd.println("");
//#ifdef PIN_KEY_UP // Maix-go
  lcd.println("UP/DOWN: Move cursor");
  lcd.println("PUSH: Toggle active");
  lcd.setTextColor(COLOR_GREENYELLOW);
  lcd.print("Copyright 2020 fukuen");
//#else
//  lcd.println("BOOT Button: Move cursor");
//  lcd.println("LONG PRESS: Toggle active");
//  lcd.setTextColor(COLOR_GREENYELLOW);
//  lcd.print("Copyright 2020 fukuen");
//#endif
}

void drawCursor() {
  lcd.fillRect(0, 32, 11, 16 * 9, COLOR_BLACK);
  lcd.setCursor(0, (posCursor + 2) * 16);
  lcd.setTextColor(COLOR_YELLOW);
  lcd.setTextSize(2);
  lcd.print(">");
}

void toggleActive(int index) {
  if ((app_entry[index].entry_id & 1) == 1) {
    app_entry[index].entry_id = app_entry[index].entry_id & 0xfffffffe;
    writeActive(index, 0);
    lcd.setCursor(12, (index + 2) * 16);
    lcd.setTextColor(COLOR_RED);
    lcd.setTextSize(2);
    lcd.print(" ");
    lcd.fillRect(12, (index + 2) * 16, 12, 16, COLOR_BLACK);
  } else {
    app_entry[index].entry_id = app_entry[index].entry_id | 1;
    writeActive(index, 1);
    lcd.setCursor(12, (index + 2) * 16);
    lcd.setTextColor(COLOR_RED);
    lcd.setTextSize(2);
    lcd.print("*");
  }
}

void setup() {
  pll_init();
  uarths_init();
  plic_init();

  lcd.begin(15000000, COLOR_RED);
  printf("start\n");

  checkKboot();
  readEntry();

  drawMenu();
  drawCursor();
}

void loop() {
//  BtnBOOT.read();
  BtnA.read();
  BtnB.read();
  BtnC.read();
//#ifdef PIN_KEY_UP // Maix-go
  if (BtnA.wasPressed()) {
      posCursor--;
      if (posCursor < 0) {
        posCursor = BOOT_CONFIG_ITEMS - 1;
      }
      drawCursor();
  }
  if (BtnB.wasPressed()) {
    toggleActive(posCursor);
  }
  if (BtnC.wasPressed()) {
      posCursor++;
      if (posCursor > BOOT_CONFIG_ITEMS - 1) {
        posCursor = 0;
      }
      drawCursor();
  }
//#else
//  if (BtnBOOT.pressedFor(2000)) {
//    btnLongPressed = true;
//    lcd.fillRect(0, 32, 11, 16 * 9, COLOR_RED);
//  }
//  if (BtnBOOT.wasReleased()) {
//    if (btnLongPressed) {
//      btnLongPressed = false;
//      posLaunch = posCursor;
//      drawLaunch();
//    } else {
//      posCursor++;
//      if (posCursor > BOOT_CONFIG_ITEMS - 1) {
//        posCursor = 0;
//      }
//      drawCursor();
//    }
//  }
//#endif
}