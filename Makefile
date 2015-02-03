OBJS = gem.o motion.o

gem: $(OBJS)
	$(CC) -o $@ $^ -lev

clean:
	rm -f $(OBJS) gem
