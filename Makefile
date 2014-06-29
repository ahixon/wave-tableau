record: record.c
	gcc -lportaudio -lSDL $< -o $@ -I/usr/include/SDL/ -g -lpthread