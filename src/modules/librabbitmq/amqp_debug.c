#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>

static void dump_row(long count, int numinrow, int *chs) {
  int i;

  printf("%08lX:", count - numinrow);

  if (numinrow > 0) {
    for (i = 0; i < numinrow; i++) {
      if (i == 8)
	printf(" :");
      printf(" %02X", chs[i]);
    }
    for (i = numinrow; i < 16; i++) {
      if (i == 8)
	printf(" :");
      printf("   ");
    }
    printf("  ");
    for (i = 0; i < numinrow; i++) {
      if (isprint(chs[i]))
	printf("%c", chs[i]);
      else
	printf(".");
    }
  }
  printf("\n");
}

static int rows_eq(int *a, int *b) {
  int i;

  for (i=0; i<16; i++)
    if (a[i] != b[i])
      return 0;

  return 1;
}

void amqp_dump(void const *buffer, size_t len) {
  unsigned char *buf = (unsigned char *) buffer;
  long count = 0;
  int numinrow = 0;
  int chs[16];
  int oldchs[16];
  int showed_dots = 0;
  int i;

  for (i = 0; i < len; i++) {
    int ch = buf[i];

    if (numinrow == 16) {
      int i;

      if (rows_eq(oldchs, chs)) {
	if (!showed_dots) {
	  showed_dots = 1;
	  printf("          .. .. .. .. .. .. .. .. : .. .. .. .. .. .. .. ..\n");
	}
      } else {
	showed_dots = 0;
	dump_row(count, numinrow, chs);
      }

      for (i=0; i<16; i++)
	oldchs[i] = chs[i];

      numinrow = 0;
    }

    count++;
    chs[numinrow++] = ch;
  }

  dump_row(count, numinrow, chs);

  if (numinrow != 0)
    printf("%08lX:\n", count);
}
