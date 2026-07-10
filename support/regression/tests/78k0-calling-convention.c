/* Regression coverage for the 78K0 calling convention. */

#include <testfwk.h>

#ifdef __SDCC_78k0
static unsigned char
first8 (unsigned char first, unsigned char second)
{
  volatile unsigned char frame[300];

  frame[0] = first;
  frame[299] = second;
  return frame[0] ^ frame[299];
}

static unsigned int
first16 (unsigned int first, unsigned char second)
{
  volatile unsigned char frame[300];

  frame[0] = second;
  return first + frame[0];
}

static unsigned long
first32 (unsigned long first, unsigned int second)
{
  volatile unsigned char frame[300];

  frame[0] = (unsigned char)second;
  return first + second + frame[0];
}
#endif

void
testCallingConvention78K0 (void)
{
#ifdef __SDCC_78k0
  ASSERT (first8 (0xa5, 0x3c) == 0x99);
  ASSERT (first16 (0x1234, 0x56) == 0x128a);
  ASSERT (first32 (0x12345678ul, 0x2345) == 0x12347a02ul);
  ASSERT (first8 (0x5a, 0xc3) == 0x99);
#endif
}
