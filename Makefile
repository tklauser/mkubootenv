
MKUBOOTENV = mkubootenv
MKUBOOTENV_OBJS = mkubootenv.o crc32.o

CFLAGS += -W -Wall -Wextra -Wstrict-prototypes -Wsign-compare -Wshadow \
	  -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
	  -Wpointer-arith -Wcast-align

all: $(MKUBOOTENV)

$(MKUBOOTENV): $(MKUBOOTENV_OBJS)
	@echo "  LD $@"
	@$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c %.h
	@echo "  CC $@"
	@$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	@echo "  CC $@"
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "  CLEAN"
	@rm -f *.o $(MKUBOOTENV)
