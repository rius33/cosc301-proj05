# variables and directives that get used in the makefile
CC = clang
CFLAGS = -g -Wall -DDEBUG=1
CPPFLAGS = 
PROGRAMS = dos_ls dos_cp dos_cat scandisk
COMMONOBJ = dos.o
.PHONY : clean

all: $(PROGRAMS)

dos_ls: %: %.o $(COMMONOBJ)
	$(CC) -o $@ $< $(COMMONOBJ) $(CFLAGS)

dos_cp: %: %.o $(COMMONOBJ)
	$(CC) -o $@ $< $(COMMONOBJ) $(CFLAGS)

dos_cat: %: %.o $(COMMONOBJ)
	$(CC) -o $@ $< $(COMMONOBJ) $(CFLAGS)

scandisk: %: %.o $(COMMONOBJ)
	$(CC) -o $@ $< $(COMMONOBJ) $(CFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

clean:
	rm -f *.o $(PROGRAMS) *~

