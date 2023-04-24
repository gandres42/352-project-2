CC = gcc 
LDLIBS = -lpthread

objects = api.o application.o data_block.o dir.o inode.o open_file_table.o
App = app

test: $(App)
	$(CC) -o $(App) $(objects) $(LDLIBS)
	./app | tee output.txt
	./test

all: $(App)

$(App): $(objects)
	$(CC) -o $(App) $(objects) -g $(LDLIBS)

$(objects): %.o: %.c 

clean:
	rm -f *.o app 