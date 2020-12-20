CC := gcc
PHPDIR ?= /opt/phpts
PHPINC := $(PHPDIR)/include/php
CFLAGS := -O2 -I$(PHPINC) -I$(PHPINC)/ext -I$(PHPINC)/main -I$(PHPINC)/sapi -I$(PHPINC)/TSRM -I$(PHPINC)/Zend $(CFLAGS)
LDFLAGS := -L$(PHPDIR)/lib -Wl,-rpath,$(PHPDIR)/lib -lphp -pthread $(LDFLAGS)

all: $(PHPINC)
$(PHPINC):
	$(error The $@ is not directory. Please usage "make PHPDIR=<dir>" by php dir, for example: /opt/phpts)

all: $(PHPDIR)/lib/libphp.so
$(PHPDIR)/lib/libphp.so:
	$(error The $@ is not directory. Please usage "make PHPDIR=<dir>" by php dir, for example: /opt/phpts)

all: threadtask
threadtask: main.o func.o hash.o
	@echo LD $@
	@$(CC) -o $@ $^ $(LDFLAGS)

func.o: func.h

%.o: %.c
	@echo CC $@
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@LANG=en rm -vf *.o threadtask
