
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argmanip.h"
#include <stdarg.h>

char **manipulate_args(int argc, const char *const *argv, int (*manip)(int)) {
    // make a copy with the equivalent allocated space
    // +1 account for the null terminator 
    char **copy = (char **)malloc((argc + 1) * sizeof(char *));
    if (!copy) {
        return NULL; // check point for failed copy
    }
    
    // loop over the arguments
    for (int i=0; i < argc; i++) {
        // set string length of the current argument
        int argument_length = strlen(argv[i]);

        // call malloc() again for each argument

        copy[i] = (char *)malloc((argument_length + 1) * sizeof(char));

        // loop through each string character-by-character, passing through the manip function
        // inner loop do need to manipulate the null terminator
        for (int j=0; j < argument_length; j++) {
            copy[i][j] = manip((char) argv[i][j]);
            }
            // add a null terminator at the end of the arg
            copy[i][argument_length] = '\0';  
        }
    copy[argc] = NULL;
    // need to return in the form argv
    return copy;
    }

void free_copied_args(char** fmt, ...) {
    // variadoc functions
    va_list args;
    va_start(args, fmt); // initialize a list of variadic argument
    char **arg_list = fmt; 

    while (arg_list != NULL) { // not *fmt != '\0' because the last argument is NULL, not string printf
        char **original_pointer = arg_list;

        while (*arg_list != NULL) {
            // free each inidividual argument
            free(*arg_list);
            arg_list++; // increment pointer
        }
        // free the entire array
        free(original_pointer);
        arg_list = va_arg(args, char **);
    }
    va_end(args);
}

// RUNNING & TESTING MAKEFILE:
// docker run -i --name cs202 --privileged --rm -t -v ~/Desktop/cs202/labs:/cs202 -w /cs202 ytang/os bash
// cd /cs202/nyuc


// RUNNING & TESTING UNDER Valgrind
// valgrind --leak-check=full --track-origins=yes ./nyuc Hello, world

// USING GDB
// gdb ./nyuc
// run Hello, World
// backtrace


// ---- DEBUG SPECIFIC LINE (September 15 - error occured in strlen) -------
// break 10 if *p == 0x0 || *q == 0x0
/*
    Breakpoint 2 at 0x4007f4: file nyuc.c, line 10.
    (gdb) run
    The program being debugged has been started already.
    Start it from the beginning? (y or n) y
    Starting program: /cs202/nyuc/nyuc 
    Error in testing breakpoint condition:
    Cannot access memory at address 0x0

    Breakpoint 2, main (argc=1, argv=0xffffffffeb18) at nyuc.c:10
    10        for (char *const *p = upper_args, *const *q = lower_args; *p && *q; ++argv, ++p, ++q) {
    (gdb) print *argv
    $7 = 0xffffffffeda1 "/cs202/nyuc/nyuc"
    (gdb) print *p
    Cannot access memory at address 0x0
    (gdb) print *q
    $8 = 0x910003fda9bc7bfd <error: Cannot access memory at address 0x910003fda9bc7bfd>
    (gdb) 
*/

//valgrind --leak-check=full --track-origins=yes ./nyuc
