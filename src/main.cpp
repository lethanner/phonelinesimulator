#include <Arduino.h>
#include <avr/eeprom.h>
#include "config.h"
#include "PhoneLine.h"

bool onCall = false;
byte lineBusy[2] = {0, 0};
byte lineNumber[2][NUM_LENGTH];

char serialBuf[NUM_LENGTH];
// bool serialBuf_state = 0;
byte serialBuf_pos = 0;

PhoneLine line1(LINE1_PROBE, LINE1_SIGNAL, LINE2_RING, &lineBusy[1], &onCall, lineNumber[1], &Serial);
PhoneLine line2(LINE2_PROBE, LINE2_SIGNAL, LINE1_RING, &lineBusy[0], &onCall, lineNumber[0], &Serial);

void printNumbers()
{
  for (byte i = 0; i < 2; i++)
  {
    for (byte j = 0; j < NUM_LENGTH; j++)
    {
      if (lineNumber[i][j] == 0xFF)
        break;
      // 10 выводится как 0
      Serial.print((lineNumber[i][j] == 10) ? 0 : lineNumber[i][j], DEC);
      Serial.write(0x20); // пробел
    }
    Serial.println();
  }
}

void setup()
{
  Serial.begin(38400);
  Serial.println(F("INIT"));

  pinMode(LINE_CONNECT, OUTPUT);

  // загрузить номера телефонов из памяти
  eeprom_read_block((void *)&lineNumber[0], (const void *)0, NUM_LENGTH);
  eeprom_read_block((void *)&lineNumber[1], (const void *)NUM_LENGTH, NUM_LENGTH);
  printNumbers();
}

void loop()
{
  line1.serve();
  line2.serve();

  // lineBusy = line1.isOffHook() || line2.isOffHook();
  lineBusy[0] = (byte)line1.isOffHook() + (byte)line1.isDialing();
  lineBusy[1] = (byte)line2.isOffHook() + (byte)line2.isDialing();
  onCall = (line1.isOffHook() && line2.isDialing()) || (line2.isOffHook() && line1.isDialing());
  digitalWrite(LINE_CONNECT, onCall);

  // конфиг по UART
  if (Serial.available() > 0)
  {
    char ch = Serial.read();
    if (serialBuf_pos < NUM_LENGTH && (ch > 47 && ch < 58))
    { // пишем буфер цифрами, пока не переполнен
      serialBuf[serialBuf_pos++] = ch;
      Serial.write(ch); // эхо в терминале
    }

    if (serialBuf_pos > 1 && (ch == 'A' || ch == 'B')) // если прислали A или B, когда начали вводить цифры
    {
      Serial.println();
      for (byte i = 0; i < NUM_LENGTH; i++)
      {
        if (i >= serialBuf_pos)
          lineNumber[ch - 65][i] = 0xFF; // недобитые цифры забиваем как 0xFF (255 в десятичной системе)
        else
          lineNumber[ch - 65][i] = (serialBuf[i] == 48) ? 10 : (byte)(serialBuf[i] - 48); // сохраняем номер телефона, переводя цифры из ASCII в байты и заменяя 0 на 10
      }
      serialBuf_pos = 0;
      Serial.println(F("\nNumbers set"));
      // обновить номера в памяти
      eeprom_update_block((const void *)&lineNumber[0], (void *)0, NUM_LENGTH);
      eeprom_update_block((const void *)&lineNumber[1], (void *)NUM_LENGTH, NUM_LENGTH);
      printNumbers();
    }
    else if (ch < 48 || ch > 57)
    {                     // а иначе если введены не цифры от 0 до 9
      serialBuf_pos = 0;  // начинаем всё сначала
      Serial.write('\r'); // возврат каретки в терминале
    }
  }
}