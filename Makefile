CFLAGS = -Wall -D_GNU_SOURCE=1
OBJS = gem.o motion.o

gem: $(OBJS)
	$(CC) -o $@ $^ -lev -lm

clean:
	rm -f $(OBJS) gem
