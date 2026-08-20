static volatile __sfr __at (0xff00) simif;

void
_putchar (unsigned char c)
{
  simif = 'p';
  simif = c;
}

void
_initEmu (void)
{
}

void
_exitEmu (void)
{
  simif = 's';
}
