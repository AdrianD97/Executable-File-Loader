build: libso_loader.so

libso_loader.so: loader.o exec_parser.o
	gcc -shared -m32 $^ -o libso_loader.so

loader.o: loader.c
	gcc -fPIC -m32 -c $^ -o $@ #-lm

exec_parser.o: exec_parser.c
	#nasm -fPIC -f elf64 $^ -o $@
	gcc -fPIC -m32 -c $^ -o $@

clean:
	rm *.o
