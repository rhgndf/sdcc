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
static volatile __sfr __at (0xff80) high_sfr;

struct shifted_bits
{
  unsigned int padding : 3;
  unsigned int value : 10;
};

static unsigned char
andHighSfr (unsigned char value)
{
  return value & high_sfr;
}

static unsigned char
rematerializedAddress8 (void)
{
  return (unsigned char)(unsigned int)&pointer_arg_data[1];
}

static unsigned long
rematerializedAddress32 (void)
{
  return (unsigned long)(unsigned int)&pointer_arg_data[1];
}

static unsigned long long
rematerializedAddress64 (void)
{
  return (unsigned long long)(unsigned int)&pointer_arg_data[1];
}

static unsigned int
divideWordByByte (unsigned int dividend, unsigned char divisor)
{
  return dividend / divisor;
}

static void
storeWordThroughPointer (unsigned int *pointer, unsigned int value)
{
  *pointer = value;
}

static unsigned int
shiftWordByWord (unsigned int value, unsigned int count)
{
  return value << count;
}

static unsigned int
readShiftedBits (const struct shifted_bits *bits)
{
  return bits->value;
}

static void
writeShiftedBits (struct shifted_bits *bits, unsigned int value)
{
  bits->value = value;
}

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

static unsigned long
makeWide32 (unsigned char seed)
{
  return seed ? 0x12345678ul : 0;
}

static uint24_t
makeWide24 (unsigned char seed)
{
  return seed ? (uint24_t)0x123456ul : (uint24_t)0;
}

static unsigned char
checkStackWide (unsigned char marker, unsigned long value, unsigned char trailing)
{
  return marker == 0x5a && value == 0x12345678ul && trailing == 0xa5;
}

static unsigned char
callResultLogicalNot32 (unsigned char seed)
{
  return !makeWide32 (seed);
}

static unsigned char
callResultIf32 (unsigned char seed)
{
  if (makeWide32 (seed))
    return 1;
  return 0;
}

static unsigned long
callResultNegate32 (unsigned char seed)
{
  return -makeWide32 (seed);
}

static unsigned int
callResultMiddleWord32 (unsigned char seed)
{
  return (unsigned int)(makeWide32 (seed) >> 8);
}

static unsigned long
callResultWiden24 (unsigned char seed)
{
  return makeWide24 (seed);
}

static unsigned char
callResultStackPush32 (unsigned char seed)
{
  return checkStackWide (0x5a, makeWide32 (seed), 0xa5);
}

static uint24_t
negate24WithLargeFrame (uint24_t value)
{
  volatile unsigned char frame[300];

  frame[0] = 0x5a;
  frame[299] = 0xa5;
  return -value;
}

static unsigned long
negate32WithLargeFrame (unsigned long value)
{
  volatile unsigned char frame[300];

  frame[0] = 0x5a;
  frame[299] = 0xa5;
  return -value;
}

static uint24_t
leftShift24WithLargeFrame (uint24_t value, unsigned char count)
{
  volatile unsigned char frame[300];
  volatile uint24_t shifted;

  frame[0] = 0x5a;
  frame[299] = 0xa5;
  shifted = value << count;
  return shifted;
}

static unsigned long
rightShift32WithLargeFrame (unsigned long value, unsigned char count)
{
  volatile unsigned char frame[300];
  volatile unsigned long shifted;

  frame[0] = 0x5a;
  frame[299] = 0xa5;
  shifted = value >> count;
  return shifted;
}

static volatile unsigned int wideBoolSink1;
static volatile unsigned int wideBoolSink2;

static unsigned char
smallValuesLiveAcrossWideBool (unsigned long value, unsigned int first,
                               unsigned int second)
{
  unsigned long transformed = value ^ 0x01020304ul;
  unsigned int left = first + 1;
  unsigned int right = second ^ 0x5a5a;
  unsigned char zero = !transformed;

  wideBoolSink1 = left;
  wideBoolSink2 = right;
  return zero;
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

struct result1
{
  unsigned char bytes[1];
};

struct result2
{
  unsigned char bytes[2];
};

struct result5
{
  unsigned char bytes[5];
};

static struct result1
makeResult1 (unsigned char seed)
{
  struct result1 result;
  result.bytes[0] = seed;
  return result;
}

static struct result2
makeResult2 (unsigned char seed)
{
  struct result2 result;
  result.bytes[0] = seed;
  result.bytes[1] = seed ^ 0x5a;
  return result;
}

static struct result5
makeResult5 (unsigned char seed)
{
  struct result5 result;
  result.bytes[0] = seed;
  result.bytes[1] = seed + 1;
  result.bytes[2] = seed + 2;
  result.bytes[3] = seed + 3;
  result.bytes[4] = seed + 4;
  return result;
}

static struct result1
forwardResult1 (unsigned char seed)
{
  return makeResult1 (seed);
}

static struct result2
forwardResult2 (unsigned char seed)
{
  return makeResult2 (seed);
}

static struct result5
forwardResult5 (unsigned char seed)
{
  return makeResult5 (seed);
}

static struct result1 (*result1Factory) (unsigned char);
static struct result2 (*result2Factory) (unsigned char);
static struct result5 (*result5Factory) (unsigned char);

static struct result1
forwardResult1Indirect (unsigned char seed)
{
  return result1Factory (seed);
}

static struct result2
forwardResult2Indirect (unsigned char seed)
{
  return result2Factory (seed);
}

static struct result5
forwardResult5Indirect (unsigned char seed)
{
  return result5Factory (seed);
}

static volatile unsigned char observedResult5;

static struct result5
assignThenReturnResult5 (unsigned char seed)
{
  struct result5 result;
  result = makeResult5 (seed);
  observedResult5 = result.bytes[2];
  return result;
}

static const unsigned long long scalar64Value = 0x123456789abcdef0ull;

static unsigned long long
makeScalar64 (void)
{
  return scalar64Value;
}

static unsigned long long
forwardScalar64 (void)
{
  return makeScalar64 ();
}

static unsigned long long (*scalar64Factory) (void);

static unsigned long long
forwardScalar64Indirect (void)
{
  return scalar64Factory ();
}

struct payload255
{
  unsigned char bytes[255];
};

struct payload256
{
  unsigned char bytes[256];
};

struct payload257
{
  unsigned char bytes[257];
};

struct payload511
{
  unsigned char bytes[511];
};

struct payload512
{
  unsigned char bytes[512];
};

#define GUARDED_PAYLOAD(size) \
  struct guarded##size { unsigned char before; struct payload##size payload; unsigned char after; }
GUARDED_PAYLOAD (255);
GUARDED_PAYLOAD (256);
GUARDED_PAYLOAD (257);
GUARDED_PAYLOAD (511);
GUARDED_PAYLOAD (512);

static struct guarded255 value255;
static struct guarded256 value256;
static struct guarded257 value257;
static struct guarded511 value511;
static struct guarded512 value512;

static unsigned char
checkPayload255 (unsigned char first, unsigned char leading,
                 struct payload255 value, unsigned char trailing)
{
  return first == 0x15 && leading == 0xc1 && trailing == 0x51 && value.bytes[0] == 0x11 &&
         value.bytes[1] == 0 && value.bytes[127] == 0x22 && value.bytes[254] == 0x33;
}

static unsigned char
checkPayload256 (unsigned int first, unsigned char leading,
                 struct payload256 value, unsigned char trailing)
{
  return first == 0x1625 && leading == 0xc2 && trailing == 0x52 && value.bytes[0] == 0x44 &&
         value.bytes[1] == 0 && value.bytes[128] == 0x55 && value.bytes[255] == 0x66;
}

static unsigned char
checkPayload257 (unsigned long first, unsigned char leading,
                 struct payload257 value, unsigned char trailing)
{
  return first == 0x17263545ul && leading == 0xc3 && trailing == 0x53 && value.bytes[0] == 0x77 &&
         value.bytes[1] == 0 && value.bytes[128] == 0x88 && value.bytes[256] == 0x99;
}

static unsigned char
checkPayload511 (unsigned char first, unsigned char leading,
                 struct payload511 value, unsigned char trailing)
{
  return first == 0x18 && leading == 0xc4 && trailing == 0x54 && value.bytes[0] == 0xaa &&
         value.bytes[1] == 0 && value.bytes[255] == 0xbb && value.bytes[510] == 0xcc;
}

static unsigned char
checkPayload512 (unsigned int first, unsigned char leading,
                 struct payload512 value, unsigned char trailing)
{
  return first == 0x1928 && leading == 0xc5 && trailing == 0x55 && value.bytes[0] == 0xdd &&
         value.bytes[1] == 0 && value.bytes[256] == 0xee && value.bytes[511] == 0xff;
}

static unsigned char
stackCallCanary (unsigned char first, unsigned int second, unsigned long third)
{
  return first == 0xa5 && second == 0x5aa5 && third == 0x12345678ul;
}

static unsigned char
callTarget (unsigned char value)
{
  return value ^ 0x5a;
}
static unsigned char (*indirectCallTarget) (unsigned char);
static const unsigned char callData[2] = {0x31, 0x42};

static unsigned char
callThroughParameter (unsigned char (*target) (unsigned char), unsigned char value)
{
  return target (value);
}

static unsigned int
pointerLiveAcrossDirectCall (const unsigned char *pointer, unsigned char value)
{
  unsigned char first = pointer[0];
  unsigned char called = callTarget (value);
  return (unsigned int)first + pointer[1] + called;
}

static unsigned int
pointerLiveAcrossIndirectCall (const unsigned char *pointer, unsigned char value)
{
  unsigned char first = pointer[0];
  unsigned char called = indirectCallTarget (value);
  return (unsigned int)first + pointer[1] + called;
}

static unsigned char
wideArgumentWithFrame (uint24_t value)
{
  volatile unsigned char frame[1];

  frame[0] = (unsigned char)value;
  return frame[0];
}

static unsigned char
wideArgumentWithSimpleFrame (uint24_t value)
{
  volatile unsigned char frame[1];

  frame[0] = 0x5a;
  return (unsigned char)value ^ frame[0];
}

/* These naked helpers intentionally seed and observe the callee-save DE pair
   so the two preservation regressions below do not depend on allocation. */
static void
seedDESentinel (void) __naked
{
  __asm
    movw de,#0x5aa5
    ret
  __endasm;
}

static unsigned int
readDESentinel (void) __naked
{
  __asm
    movw ax,de
    ret
  __endasm;
}

static unsigned int
deAfterWideFrameCall (void)
{
  seedDESentinel ();
  wideArgumentWithSimpleFrame ((uint24_t)0x123456ul);
  return readDESentinel ();
}

static volatile unsigned char wideResultSentinelValue;
static volatile unsigned char stackByteSentinelValue;

static unsigned int
deAfterWideResultSpill (void)
{
  unsigned long called;
  unsigned int preserved;

  seedDESentinel ();
  called = makeWide32 (1);
  preserved = readDESentinel ();
  wideResultSentinelValue = (unsigned char)called;
  return preserved;
}

static unsigned int
deAfterStackByteBinary (void)
{
  volatile unsigned char stack_byte = 0x0f;
  unsigned char result;
  unsigned int preserved;

  seedDESentinel ();
  result = 0xf0 - stack_byte;
  preserved = readDESentinel ();
  stackByteSentinelValue = result;
  return preserved;
}

static unsigned int
pointerLiveAcrossWideFrameCall (const unsigned char *pointer, uint24_t value)
{
  unsigned char first = pointer[0];
  unsigned char called = wideArgumentWithFrame (value);
  return (unsigned int)first + pointer[1] + called;
}

static unsigned int
pointerLiveAcrossWideResultCall (const unsigned char *pointer, unsigned char seed)
{
  unsigned char first = pointer[0];
  unsigned long called = makeWide32 (seed);
  return (unsigned int)first + pointer[1] + (unsigned char)called;
}

static volatile unsigned int wideFrameCallSink;
static volatile unsigned int wideResultCallSink;

static unsigned char
valueLiveAcrossWideFrameCall (unsigned int value, uint24_t argument)
{
  unsigned int survivor = value + 1;
  unsigned char called = wideArgumentWithFrame (argument);

  wideFrameCallSink = survivor;
  return called;
}

static unsigned char
valueLiveAcrossWideResultCall (unsigned int value, unsigned char seed)
{
  unsigned int survivor = value + 1;
  unsigned long called = makeWide32 (seed);

  wideResultCallSink = survivor;
  return (unsigned char)called;
}

static volatile unsigned char interruptData[2];
static volatile unsigned char interruptResult;
static volatile unsigned char *interruptPointer = interruptData;

void
aggregateAbiInterrupt (void) __interrupt (1)
{
  volatile unsigned char *pointer = interruptPointer;
  unsigned char first = pointer[0];
  unsigned char called = callTarget (first);
  interruptResult = called + pointer[1];
}
#endif

void
testCallingConvention78K0 (void)
{
#ifdef __SDCC_78k0
  union float_bits negative_zero;
  struct shifted_bits bits = {0, 0};
  unsigned int stored_word = 0;
  unsigned int pointer_address = (unsigned int)&pointer_arg_data[1];

  negative_zero.bits = 0x80000000ul;
  high_sfr = 0x5a;
  ASSERT (andHighSfr (0x3c) == 0x18);
  ASSERT (rematerializedAddress8 () == (unsigned char)pointer_address);
  ASSERT (rematerializedAddress32 () == (unsigned long)pointer_address);
  ASSERT (rematerializedAddress64 () == (unsigned long long)pointer_address);
  ASSERT (divideWordByByte (0x1234, 0x12) == 0x0102);
  storeWordThroughPointer (&stored_word, 0x5aa5);
  ASSERT (stored_word == 0x5aa5);
  ASSERT (shiftWordByWord (0x1234, 4) == 0x2340);
  writeShiftedBits (&bits, 0x02d5);
  ASSERT (readShiftedBits (&bits) == 0x02d5);
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
  ASSERT (callResultLogicalNot32 (0) == 1);
  ASSERT (callResultLogicalNot32 (1) == 0);
  ASSERT (callResultIf32 (0) == 0);
  ASSERT (callResultIf32 (1) == 1);
  ASSERT (callResultNegate32 (1) == 0xedcba988ul);
  ASSERT (callResultMiddleWord32 (1) == 0x3456);
  ASSERT (callResultWiden24 (1) == 0x00123456ul);
  ASSERT (callResultStackPush32 (1));
  ASSERT (negate24WithLargeFrame ((uint24_t)0x123456ul) == (uint24_t)0xedcbaaUL);
  ASSERT (negate32WithLargeFrame (0x12345678ul) == 0xedcba988ul);
  ASSERT (leftShift24WithLargeFrame ((uint24_t)0x123456ul, 5) == (uint24_t)0x468ac0ul);
  ASSERT (rightShift32WithLargeFrame (0x87654321ul, 7) == 0x010eca86ul);
  ASSERT (smallValuesLiveAcrossWideBool (0x01020304ul, 0x1000, 0x2000) == 1);
  ASSERT (wideBoolSink1 == 0x1001 && wideBoolSink2 == 0x7a5a);
  ASSERT (smallValuesLiveAcrossWideBool (0x01020305ul, 0x1000, 0x2000) == 0);
  ASSERT (wideBoolSink1 == 0x1001 && wideBoolSink2 == 0x7a5a);
  ASSERT (oneByteFrame (0xa5) == 0xa5);
  ASSERT (twoByteFrame (0x5aa5) == 0x5aa5);
#endif
}

void
testAggregateAbi78K0 (void)
{
#ifdef __SDCC_78k0
  struct result1 result1;
  struct result2 result2;
  struct result5 result5;

  result1Factory = makeResult1;
  result2Factory = makeResult2;
  result5Factory = makeResult5;
  scalar64Factory = makeScalar64;
  indirectCallTarget = callTarget;

  ASSERT (sizeof (struct result1) == 1);
  ASSERT (sizeof (struct result2) == 2);
  ASSERT (sizeof (struct result5) == 5);
  ASSERT (sizeof (struct payload255) == 255);
  ASSERT (sizeof (struct payload256) == 256);
  ASSERT (sizeof (struct payload257) == 257);
  ASSERT (sizeof (struct payload511) == 511);
  ASSERT (sizeof (struct payload512) == 512);

  result1 = forwardResult1 (0x21);
  ASSERT (result1.bytes[0] == 0x21);
  result2 = forwardResult2 (0x32);
  ASSERT (result2.bytes[0] == 0x32 && result2.bytes[1] == 0x68);
  result5 = forwardResult5 (0x43);
  ASSERT (result5.bytes[0] == 0x43 && result5.bytes[2] == 0x45 && result5.bytes[4] == 0x47);
  result1 = forwardResult1Indirect (0x54);
  ASSERT (result1.bytes[0] == 0x54);
  result2 = forwardResult2Indirect (0x65);
  ASSERT (result2.bytes[0] == 0x65 && result2.bytes[1] == 0x3f);
  result5 = forwardResult5Indirect (0x76);
  ASSERT (result5.bytes[0] == 0x76 && result5.bytes[2] == 0x78 && result5.bytes[4] == 0x7a);

  result5 = assignThenReturnResult5 (0x87);
  ASSERT (observedResult5 == 0x89);
  ASSERT (result5.bytes[0] == 0x87 && result5.bytes[2] == 0x89 && result5.bytes[4] == 0x8b);
  ASSERT (forwardScalar64 () == scalar64Value);
  ASSERT (forwardScalar64Indirect () == scalar64Value);

  value255.before = 0xa1;
  value255.after = 0x1a;
  value255.payload.bytes[0] = 0x11;
  value255.payload.bytes[127] = 0x22;
  value255.payload.bytes[254] = 0x33;
  ASSERT (checkPayload255 (0x15, 0xc1, value255.payload, 0x51));
  ASSERT (value255.before == 0xa1 && value255.after == 0x1a);
  ASSERT (stackCallCanary (0xa5, 0x5aa5, 0x12345678ul));

  value256.before = 0xa2;
  value256.after = 0x2a;
  value256.payload.bytes[0] = 0x44;
  value256.payload.bytes[128] = 0x55;
  value256.payload.bytes[255] = 0x66;
  ASSERT (checkPayload256 (0x1625, 0xc2, value256.payload, 0x52));
  ASSERT (value256.before == 0xa2 && value256.after == 0x2a);
  ASSERT (stackCallCanary (0xa5, 0x5aa5, 0x12345678ul));

  value257.before = 0xa3;
  value257.after = 0x3a;
  value257.payload.bytes[0] = 0x77;
  value257.payload.bytes[128] = 0x88;
  value257.payload.bytes[256] = 0x99;
  ASSERT (checkPayload257 (0x17263545ul, 0xc3, value257.payload, 0x53));
  ASSERT (value257.before == 0xa3 && value257.after == 0x3a);
  ASSERT (stackCallCanary (0xa5, 0x5aa5, 0x12345678ul));

  value511.before = 0xa4;
  value511.after = 0x4a;
  value511.payload.bytes[0] = 0xaa;
  value511.payload.bytes[255] = 0xbb;
  value511.payload.bytes[510] = 0xcc;
  ASSERT (checkPayload511 (0x18, 0xc4, value511.payload, 0x54));
  ASSERT (value511.before == 0xa4 && value511.after == 0x4a);
  ASSERT (stackCallCanary (0xa5, 0x5aa5, 0x12345678ul));

  value512.before = 0xa5;
  value512.after = 0x5a;
  value512.payload.bytes[0] = 0xdd;
  value512.payload.bytes[256] = 0xee;
  value512.payload.bytes[511] = 0xff;
  ASSERT (checkPayload512 (0x1928, 0xc5, value512.payload, 0x55));
  ASSERT (value512.before == 0xa5 && value512.after == 0x5a);
  ASSERT (stackCallCanary (0xa5, 0x5aa5, 0x12345678ul));

  ASSERT (pointerLiveAcrossDirectCall (callData, 0x24) == 0x00f1);
  ASSERT (pointerLiveAcrossIndirectCall (callData, 0x24) == 0x00f1);
  ASSERT (pointerLiveAcrossWideFrameCall (callData, (uint24_t)0x123456ul) == 0x00c9);
  ASSERT (pointerLiveAcrossWideResultCall (callData, 1) == 0x00eb);
  ASSERT (valueLiveAcrossWideFrameCall (0x1234, (uint24_t)0x123456ul) == 0x56);
  ASSERT (wideFrameCallSink == 0x1235);
  ASSERT (valueLiveAcrossWideResultCall (0x1234, 1) == 0x78);
  ASSERT (wideResultCallSink == 0x1235);
  ASSERT (deAfterWideFrameCall () == 0x5aa5);
  ASSERT (deAfterWideResultSpill () == 0x5aa5);
  ASSERT (wideResultSentinelValue == 0x78);
  ASSERT (deAfterStackByteBinary () == 0x5aa5);
  ASSERT (stackByteSentinelValue == 0xe1);
  ASSERT (callThroughParameter (callTarget, 0x24) == 0x7e);
#endif
}
