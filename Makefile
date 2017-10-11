all: yashd yashc

# The program we wat to build and what it depends on and how to build it
yashd: yashd.o
	gcc -o yashd yashd.o -lpthread


yashd.o: yashd.c
	gcc -c yashd.c 

yashc: yashc.o
	gcc -o yashc yashc.o

yashc.o: yashd.c
	gcc -c yashc.c	

clean:
	rm *.o yashd yashc



