static volatile unsigned char *simif;

void
_putchar (unsigned char c)
{
  *simif = 'p';
  *simif = c;
}

void
_initEmu (void)
{
  simif = (volatile unsigned char *)0xff00;
}

void
_exitEmu (void)
{
  *simif = 's';
}
