#
# Makefile
# gcc -o emond emond.c sockets.c lcdproc.c config.c webapi.c -I/usr/local/include -L/usr/local/lib -lwiringPi -lrt -lcurl
#

RM	= \rm -f
PROG	= emond
BINPATH	=/usr/local/bin
CNFPATH	=/etc
SRCPATH	=./src

SRC	= emond.c sockets.c lcdproc.c config.c webapi.c
OBJ	= $(SRC:.c=.o)

# DEBUG	= -O2
CC	= gcc
INCLUDE	= -I/usr/local/include
LIBS	= -L/usr/local/lib
CFLAGS	= $(DEBUG) $(INCLUDE) $(LIBS) -Wformat=2 -Wall -Winline  -pipe -fPIC 

# List of objects files for the dependency
OBJS_DEPEND= -lwiringPi -lrt -lcurl -lpthread

# OPTIONS = --verbose

all: target

target: Makefile
	@echo "--- Compile and Link all object files to create the executable file: $(PROG) ---"
	cd $(SRCPATH); $(CC) $(SRC) -o $(PROG) $(CFLAGS) $(OBJS_DEPEND) $(OPTIONS)
	@echo ""

clean:
	@echo "---- Cleaning all object and executable files ----"
	$(RM) $(PROG) $(OBJ)
	@echo "" 

install: target
	@echo "---- Install binaries and scripts ----"
	cp $(SRCPATH)/$(PROG) $(BINPATH)
	cp conf/emon.conf $(CNFPATH)/
	cp init.d/emon $(CNFPATH)/init.d/
 
