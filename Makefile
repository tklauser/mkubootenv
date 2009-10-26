prefix = $(HOME)

P	 = mkubootenv
OBJS	 = mkubootenv.o crc32.o
WHERE	 = $(prefix)/bin/$(P)

CFLAGS	+= -W -Wall -Wextra -Wstrict-prototypes -Wsign-compare -Wshadow \
	   -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
	   -Wpointer-arith -Wcast-align

all: $(P)

$(P): $(OBJS)
	@echo "  LD $@"
	@$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c %.h
	@echo "  CC $@"
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	@echo "  CC $@"
	@$(CC) $(CFLAGS) -c $< -o $@

install:
	@echo "  INSTALL $(WHERE)"
	@install -m755 -D $(P) $(WHERE)

uninstall:
	@echo "  UNINSTALL $(WHERE)"
	@rm -f $(WHERE)

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(P)
