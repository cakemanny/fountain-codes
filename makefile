all: fountain.exe

fountain.exe: fountain.c
	gcc fountain.c -o fountain -s -O3 
