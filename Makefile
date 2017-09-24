# The program we wat to build and what it depends on and how to build it
yash: yash.o
	gcc -o yash yash.o
# hello.o depends o hello.c and its build by runnint the command gc-c hello.c
yash.o : yash.c
	gcc -c yash.c

# What to do if make is run as:
# make clean
# remove all object and executables
clean:
	rm *.o yash
