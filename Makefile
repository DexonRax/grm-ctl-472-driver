all:
	gcc main.c -o tablet-driver -lhidapi-hidraw -lX11 -lXtst
