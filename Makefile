CC := gcc
PHPDIR ?= /opt/phpts
PHPSO ?= php7
PHPINC := $(PHPDIR)/include/php
CFLAGS := -O2 -Wall -I$(PHPINC) -I$(PHPINC)/ext -I$(PHPINC)/main -I$(PHPINC)/sapi -I$(PHPINC)/TSRM -I$(PHPINC)/Zend $(CFLAGS)
LDFLAGS := -L$(PHPDIR)/lib -Wl,-rpath,$(PHPDIR)/lib -l$(PHPSO) -pthread $(LDFLAGS)

all: $(PHPINC)
	@echo -n
$(PHPINC):
	$(error The $@ is not directory. Please usage "make PHPDIR=<dir>" by php dir, for example: /opt/phpts)

all: $(PHPDIR)/lib/lib$(PHPSO).so
$(PHPDIR)/lib/lib$(PHPSO).so:
	$(error The $@ is not directory. Please usage "make PHPDIR=<dir>" by php dir, for example: /opt/phpts)

all: threadtask
threadtask: main.o func.o hash.o
	@echo LD $@
	@$(CC) -o $@ $^ $(LDFLAGS)

main.o: func.h
func.o: func.h hash.h
hash.o: hash.h

%.o: %.c
	@echo CC $@
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@LANG=en rm -vf *.o threadtask

