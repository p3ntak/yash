main.c
contains:
main
main loop
any function directly called in main loop

helpers.h
contains:
all other functions called in main.c that are not called in main loop

goals:
1. reduce to a total of one method that starts a child
2. relocate redirection methods to helper so only one line is needed in main.c
