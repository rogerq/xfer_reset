
xfer_reset: xfer_reset.c
	gcc -o xfer_reset xfer_reset.c -lusb-1.0 -lpthread

.PHONY: clean

clean:
	rm -f xfer_reset
