# The program we wat to build and what it depends on and how to build it
yashd: yashd.o
	gcc -o yashd yashd.o -lpthread

# hello.o depends o hello.c and its build by runnint the command gc-c hello.c
yashd.o : yashd.c
	gcc -c yashd.c 

# What to do if make is run as:
# make clean
# remove all object and executables
clean:
	rm *.o yashd
	rm *.o yash

