#include <utf8rus.h>
#include <Arduino.h>

uint8_t max_string = 21;
char target[22] = "";

char *utf8rus(char *source)
{
  int i,j,k;
  unsigned char n;
  char m[2] = { '0', '\0' };

  strcpy(target, ""); k = strlen(source); i = j = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xC0) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB8; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
          break;
        }
      }
    }

    m[0] = n; strcat(target, m);
    j++; if (j >= 21) break;
  }
  return target;
}
