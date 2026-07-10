/* Regression coverage for the 78K0 calling convention. */

#include <testfwk.h>

#ifdef __SDCC_78k0
typedef unsigned _BitInt(24) uint24_t;

union float_bits
{
  float value;
  unsigned long bits;
};

static const unsigned char pointer_arg_data[] = {0x5a, 0xa5};

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

static uint24_t
first24 (uint24_t first, unsigned char second)
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

static unsigned long
firstFloat (float first, unsigned char second)
{
  volatile unsigned char frame[300];
  union float_bits value;

  frame[0] = second;
  value.value = first;
  return value.bits + frame[0];
}

static unsigned char
floatLogicalNot (float value)
{
  return !value;
}

static unsigned char
floatIf (float value)
{
  if (value)
    return 1;
  return 0;
}

static unsigned char
oneByteFrame (unsigned char value)
{
  volatile unsigned char frame[1];

  frame[0] = value;
  return frame[0];
}

static unsigned int
twoByteFrame (unsigned int value)
{
  volatile unsigned char frame[2];

  frame[0] = (unsigned char)value;
  frame[1] = (unsigned char)(value >> 8);
  return (unsigned int)frame[0] | (unsigned int)frame[1] << 8;
}

static unsigned char
firstPointer (const unsigned char *first)
{
  return first[1];
}
#endif

void
testCallingConvention78K0 (void)
{
#ifdef __SDCC_78k0
  union float_bits negative_zero;

  negative_zero.bits = 0x80000000ul;
  ASSERT (first8 (0xa5, 0x3c) == 0x99);
  ASSERT (first16 (0x1234, 0x56) == 0x128a);
  ASSERT (first24 ((uint24_t)0x123456ul, 0x78) == (uint24_t)0x1234ceul);
  ASSERT (first32 (0x12345678ul, 0x2345) == 0x12347a02ul);
  ASSERT (firstFloat (1.0f, 0x12) == 0x3f800012ul);
  ASSERT (firstPointer (pointer_arg_data) == 0xa5);
  ASSERT (first8 (0x5a, 0xc3) == 0x99);
  ASSERT (floatLogicalNot (negative_zero.value) == 1);
  ASSERT (floatIf (negative_zero.value) == 0);
  ASSERT (floatLogicalNot (1.0f) == 0);
  ASSERT (floatIf (1.0f) == 1);
  ASSERT (oneByteFrame (0xa5) == 0xa5);
  ASSERT (twoByteFrame (0x5aa5) == 0x5aa5);
#endif
}
