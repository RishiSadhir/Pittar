all: pittar compress

pittar:
	gcc -o pittar pittar.c -fnested-functions

compress:
	gcc compress.c -o compress -fnested-functions

check-syntax:
	gcc -o nul -S ${CHK_SOURCES}
