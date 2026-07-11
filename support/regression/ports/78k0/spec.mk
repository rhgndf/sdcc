# Regression test specification for the 78K0 target running with k0emu

K0EMU_DIR ?= $(top_srcdir)/../../k0emu
EMU = $(PYTHON) $(PORTS_DIR)/$(PORT_BASE)/run.py --k0emu-dir $(K0EMU_DIR)
EMU_INPUT =

ifdef SDCC_BIN_PATH
  AS = $(SDCC_BIN_PATH)/sdas78k0$(EXEEXT)
else
  AS = $(top_builddir)/bin/sdas78k0$(EXEEXT)

ifndef CROSSCOMPILING
  SDCCFLAGS += --nostdinc -I$(top_srcdir)
  LINKFLAGS += --nostdlib -L$(top_builddir)/device/lib/build/78k0
endif
endif

ifdef CROSSCOMPILING
  SDCCFLAGS += -I$(top_srcdir)
endif

SDCCFLAGS += -m78k0 -DSTACK_SIZE=256 --less-pedantic --out-fmt-ihx --data-loc 0xc000 --idata-loc 0xc000 --stack-loc 0xf000
LINKFLAGS += 78k0.lib

OBJEXT = .rel
BINEXT = .ihx

# Otherwise make deletes testfwk.rel and parallel test runs can fail.
.PRECIOUS: $(PORT_CASES_DIR)/%$(OBJEXT)

EXTRAS = $(PORT_CASES_DIR)/testfwk$(OBJEXT) $(PORT_CASES_DIR)/support$(OBJEXT)
include $(srcdir)/fwk/lib/spec.mk

%$(OBJEXT): %.asm
	$(AS) -plosgff $<

_clean:
