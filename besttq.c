#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* CITS2002 Project 1 2019
   Name(s):             Bradan Beaver, Brandon Barker
   Student number(s):   22483733, 22507204
 */

//    Notes:
//        Line 89: Check if word 3 needs to be bytes/sec
//        Line 0: Formatting - 80 wide columns?

//  besttq (v1.0)
//  Written by Chris.McDonald@uwa.edu.au, 2019, free for all to copy and modify

//  Compile with:  cc -std=c99 -Wall -Werror -o besttq besttq.c


//  THESE CONSTANTS DEFINE THE MAXIMUM SIZE OF TRACEFILE CONTENTS (AND HENCE
//  JOB-MIX) THAT YOUR PROGRAM NEEDS TO SUPPORT.  YOU'LL REQUIRE THESE
//  CONSTANTS WHEN DEFINING THE MAXIMUM SIZES OF ANY REQUIRED DATA STRUCTURES.

#define MAX_DEVICES             4
#define MAX_DEVICE_NAME         20
#define MAX_PROCESSES           50
// DO NOT USE THIS - #define MAX_PROCESS_EVENTS      1000
#define MAX_EVENTS_PER_PROCESS  100

#define TIME_CONTEXT_SWITCH     5
#define TIME_ACQUIRE_BUS        5


//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

int optimal_time_quantum                = 0;
int total_process_completion_time       = 0;
int time_elapsed                        = 0;

//  DEVICE VARIABLES
char device_names[MAX_DEVICES][MAX_DEVICE_NAME];
int device_rates[MAX_DEVICES];
int device_count = 0;

//  PROCESSES ARRAY INDEX
#define PROCESS_START           0
#define PROCESS_EXIT            1
#define PROCESS_EVENT           2
#define PROCESS_EVENT_START     3
#define PROCESS_STATE           4
#define PROCESS_ELAPSED         5
#define PROCESS_ID              6

//  PROCESS STATES
#define NEW         0
#define READY       1
#define RUNNING     2
#define BLOCKED     3
#define EXIT        4

int processes[MAX_PROCESSES][7];
int process_count = 0;
int process_window = 0;

//  EVENTS ARRAY INDEX
#define EVENT_PROCESS       0
#define EVENT_START         1
#define EVENT_END           2
#define EVENT_DEVICE        3
#define EVENT_TIME          4

int events[MAX_EVENTS_PER_PROCESS*MAX_PROCESSES][5];
int event_count = 0;
int event_window = 0;

//  ----------------------------------------------------------------------

#define CHAR_COMMENT            '#'
#define MAXWORD                 20

void parse_tracefile(char program[], char tracefile[])
{
//  ATTEMPT TO OPEN OUR TRACEFILE, REPORTING AN ERROR IF WE CAN'T
    FILE *fp    = fopen(tracefile, "r");

    if(fp == NULL) {
        printf("%s: unable to open '%s'\n", program, tracefile);
        exit(EXIT_FAILURE);
    }

    char line[BUFSIZ];
    int  lc     = 0;

//  READ EACH LINE FROM THE TRACEFILE, UNTIL WE REACH THE END-OF-FILE
    while(fgets(line, sizeof line, fp) != NULL) {
        ++lc;

//  COMMENT LINES ARE SIMPLY SKIPPED
        if(line[0] == CHAR_COMMENT) {
            continue;
        }

//  ATTEMPT TO BREAK EACH LINE INTO A NUMBER OF WORDS, USING sscanf()
        char    word0[MAXWORD], word1[MAXWORD], word2[MAXWORD], word3[MAXWORD];
        int nwords = sscanf(line, "%s %s %s %s", word0, word1, word2, word3);

//      printf("%i = %s", nwords, line);

//  WE WILL SIMPLY IGNORE ANY LINE WITHOUT ANY WORDS
        if(nwords <= 0) {
            continue;
        }
//  LOOK FOR LINES DEFINING DEVICES, PROCESSES, AND PROCESS EVENTS
        if(nwords == 4 && strcmp(word0, "device") == 0) {
            strcpy(device_names[device_count], word1);
            device_rates[device_count++] = atoi(word2);
        }

        else if(nwords == 1 && strcmp(word0, "reboot") == 0) {
            ;   // NOTHING REALLY REQUIRED, DEVICE DEFINITIONS HAVE FINISHED
        }

        else if(nwords == 4 && strcmp(word0, "process") == 0) {
            processes[process_count][PROCESS_START] = atoi(word2);
            processes[process_count][PROCESS_ID] = atoi(word1);
            processes[process_count][PROCESS_EVENT_START] = 
                processes[process_count][PROCESS_EVENT] = 
                -1;
        }

        else if(nwords == 4 && strcmp(word0, "i/o") == 0) {
            events[event_count][EVENT_START] = atoi(word1);
            events[event_count][EVENT_PROCESS] = process_count;

//  FINDING AND STORING THE INDEX OF THE EVENT'S CORRESPONDING DEVICE
//  THEN CALCULATING TOTAL RUN TIME
            for(int i = 0; i < device_count; i++) {
                if(strcmp(word2, device_names[i]) == 0) {
                    events[event_count][EVENT_DEVICE] = i;
                    events[event_count][EVENT_TIME] = 
                        ceil(atoi(word3)/(device_rates[i]/1000000.0));
                }
            }

            if(processes[process_count][PROCESS_EVENT_START] == -1) {
                processes[process_count][PROCESS_EVENT] = event_count;
                processes[process_count][PROCESS_EVENT_START] = event_count;
            }
            event_count++;
        }

        else if(nwords == 2 && strcmp(word0, "exit") == 0) {
            processes[process_count][PROCESS_EXIT] = atoi(word1);
        }

        else if(nwords == 1 && strcmp(word0, "}") == 0) {
            process_count++;
        }

        else {
            printf("%s: line %i of '%s' is unrecognized",
                        program, lc, tracefile);
            exit(EXIT_FAILURE);
        }
    }

    fclose(fp);
}

#undef  MAXWORD
#undef  CHAR_COMMENT

//  ----------------------------------------------------------------------

//  FIND THE NEXT UNFINISHED ITEM IN THE QUEUE
void next_unfinished_item() {
    for(int i = 0; i < process_count; i++) {
        process_window = (process_window + 1) % process_count;
        if(processes[process_window][PROCESS_STATE] == READY) {
                break;
        }
    }
    for(int i = 0; i < process_count; i++) {
        process_window = (process_window + 1) % process_count;
        if(processes[process_window][PROCESS_STATE] == RUNNING) {
                break;
        }
    }
}

//  CHECK EACH PROCESS FOR ASYNC ACTIONS
void asynchronous_actions() {

    for(int i = 0; i < process_count; i++) {

//  CHECK FOR NEW PROCESSES
        if(processes[i][PROCESS_START] == time_elapsed) {

            printf("@%08i\tProcess %i\tNEW -> READY\n", time_elapsed, 
                processes[i][PROCESS_ID]);

            processes[i][PROCESS_STATE] = READY;
        }

//  CHECK FOR COMPLETED I/O EVENTS
        if(processes[i][PROCESS_STATE] == BLOCKED) {
            
            int temp_event = processes[i][PROCESS_EVENT];
            
            if(events[temp_event][EVENT_END] == time_elapsed) {
                
                processes[i][PROCESS_STATE] = READY;
                
                if(events[++temp_event][EVENT_PROCESS] != i) {
                    temp_event = -1;
                }

                processes[i][PROCESS_EVENT] = temp_event;

                printf("@%08i\tProcess %i\tBLOCKED(%s) -> READY\n", 
                    time_elapsed, processes[i][PROCESS_ID], 
                    device_names[events[event_window][EVENT_DEVICE]]);

                next_unfinished_item();
            }
        }

    }   

}

void get_max_event() {
    //  LOOK FOR ANY EVENTS THAT WILL END AFTER THE CURRENT ELAPSED TIME
    int max = time_elapsed;

    events[event_window][EVENT_END] = 
        max + events[event_window][EVENT_TIME] + 5;

    for(int i = 1; i < event_count; i++) {
        event_window = (event_window + 1) % event_count;
        if(events[event_window][EVENT_END] >= max) {
            max = events[event_window][EVENT_END];
        }
    }
                
    event_window++;

//  IF AN EVENT EXISTS, ADD THE CURRENT EVENT'S RUNTIME TO IT
    if(max != time_elapsed) {
        events[event_window][EVENT_END] = 
            max + events[event_window][EVENT_TIME] + 5;
    }
}

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, FOR THE GIVEN TIME-QUANTUM
void simulate_job_mix(int time_quantum)
{
    printf("running simulate_job_mix( time_quantum = %i usecs )\n",
                time_quantum);

//  RESET QUEUE VARIABLES
    process_window = 0;
    event_window = 0;
    time_elapsed = 0;
    int tq_elapsed = 0;
    int time_to_action = 0;
    bool simulation_unfinished = true;

//  RESET ALL PROCESSES
    for(int i = 0; i < process_count; i++) {
        processes[i][PROCESS_STATE] = NEW;
        processes[i][PROCESS_ELAPSED] = 0;
        processes[i][PROCESS_EVENT] = processes[i][PROCESS_EVENT_START];
    }

//  RESET ALL EVENTS
    for(int i = 0; i < event_count; i++) {
        events[i][EVENT_END] = 0;
    }


    while(simulation_unfinished) {

        time_elapsed++;
        event_window = processes[process_window][PROCESS_EVENT];
        
//  CHECK WHAT THE CURRENT PROCESS CAN DO
//  RUNNING
        if(processes[process_window][PROCESS_STATE] == RUNNING) {

            processes[process_window][PROCESS_ELAPSED]++;
            tq_elapsed++;

//  EXIT A COMPLETED PROCESS
            if(processes[process_window][PROCESS_EXIT] 
                == processes[process_window][PROCESS_ELAPSED]) {

                printf("@%08i\tProcess %i\tRUNNING -> EXIT\n", 
                    time_elapsed, processes[process_window][PROCESS_ID]);

                processes[process_window][PROCESS_STATE] = EXIT;
                tq_elapsed = 0;
            }

//  PROCESS HAS REACHED TIME QUANTUM, SO WE EITHER...
            if(tq_elapsed == time_quantum) {

                processes[process_window][PROCESS_STATE] = READY;
                int temp_window = process_window;
                next_unfinished_item();
                tq_elapsed = 0;
                
//  MOVE TO NEXT PROCESS IN QUEUE, OR...
                if(temp_window != process_window) {

                    printf("@%08i\tProcess %i\tEXPIRE, RUNNING -> READY\n",
                        time_elapsed, processes[temp_window][PROCESS_ID]);

                    asynchronous_actions();
                    continue;
                }

//  START A FRESH TIME QUANTUM AND CONTINUE EXECUTION
                else {
                    processes[process_window][PROCESS_STATE] = RUNNING;

                    printf("@%08i\tProcess %i\tFRESH TIME QUANTUM\n",
                        time_elapsed, processes[process_window][PROCESS_ID]);
                }
            }

//  THERE IS A NEW I/O EVENT WE CAN START
            if(events[event_window][EVENT_START] == 
                processes[process_window][PROCESS_ELAPSED] && 
                event_count > 0) {

                printf("@%08i\tProcess %i\tRUNNING -> BLOCKED(%s)\n", 
                    time_elapsed, processes[process_window][PROCESS_ID],
                    device_names[events[event_window][EVENT_DEVICE]]);

                processes[process_window][PROCESS_STATE] = BLOCKED;

                get_max_event();

                next_unfinished_item();
                tq_elapsed = 0;
                asynchronous_actions();
                continue;
            }
        }

//  READY
        if(processes[process_window][PROCESS_STATE] == READY) {

//  COUNT DOWN 5 SECONDS TO GO FROM READY TO RUNNING
            time_to_action++;

            if(time_to_action == 5) {
            
                printf("@%08i\tProcess %i\tREADY -> RUNNING\n", 
                    time_elapsed, processes[process_window][PROCESS_ID]);
            
                processes[process_window][PROCESS_STATE] = RUNNING;
                time_to_action = 0;

//  CHECK TO SEE IF WE CAN START A PROCESS RIGHT AWAY
                if(events[event_window][EVENT_START] == 
                    processes[process_window][PROCESS_ELAPSED] && 
                    event_window != -1) {
                    
                    printf("@%08i\tProcess %i\tRUNNING -> BLOCKED(%s)\n", 
                        time_elapsed, processes[process_window][PROCESS_ID], 
                        device_names[events[event_window][EVENT_DEVICE]]);

                    processes[process_window][PROCESS_STATE] = BLOCKED;
                    
                    get_max_event();

                    next_unfinished_item();
                    tq_elapsed = 0;
                    asynchronous_actions();
                    continue;
                }
            }
        }
        
//  EXIT
        if(processes[process_window][PROCESS_STATE] == EXIT) {
//  FIND NEXT UNFINISHED ITEM IN QUEUE
            for(int i = 0; i < process_count; i++) {
                process_window = (process_window + 1) % process_count;
                if(processes[process_window][PROCESS_STATE] == EXIT) {
                    continue;
                }
                break;
            }

//  IF QUEUE IS COMPLETELY DONE
            if(processes[process_window][PROCESS_STATE] == EXIT) {

//  CHECK IF THIS TIME QUANTUM PERFORMS BETTER THAN THE PREVIOUS MAX
                if(time_elapsed - processes[0][PROCESS_START] <= 
                    total_process_completion_time || 
                    total_process_completion_time == 0) {
                
                    optimal_time_quantum = time_quantum;
                
                    total_process_completion_time = 
                        time_elapsed - processes[0][PROCESS_START];
                
                }
                
                simulation_unfinished = false;
            }
        }

        asynchronous_actions();
    }
    
}

//  ----------------------------------------------------------------------

void usage(char program[])
{
    printf("Usage: %s tracefile TQ-first [TQ-final TQ-increment]\n", program);
    exit(EXIT_FAILURE);
}

int main(int argcount, char *argvalue[])
{
    int TQ0 = 0, TQfinal = 0, TQinc = 0;

//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND THREE TIME VALUES
    if(argcount == 5) {
        TQ0     = atoi(argvalue[2]);
        TQfinal = atoi(argvalue[3]);
        TQinc   = atoi(argvalue[4]);

        if(TQ0 < 1 || TQfinal < TQ0 || TQinc < 1) {
            usage(argvalue[0]);
        }
    }
//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND ONE TIME VALUE
    else if(argcount == 3) {
        TQ0     = atoi(argvalue[2]);
        if(TQ0 < 1) {
            usage(argvalue[0]);
        }
        TQfinal = TQ0;
        TQinc   = 1;
    }
//  CALLED INCORRECTLY, REPORT THE ERROR AND TERMINATE
    else {
        usage(argvalue[0]);
    }

//  READ THE JOB-MIX FROM THE TRACEFILE, STORING INFORMATION IN DATA-STRUCTURES
    parse_tracefile(argvalue[0], argvalue[1]);

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, VARYING THE TIME-QUANTUM EACH TIME.
//  WE NEED TO FIND THE BEST (SHORTEST) TOTAL-PROCESS-COMPLETION-TIME
//  ACROSS EACH OF THE TIME-QUANTA BEING CONSIDERED

    for(int time_quantum=TQ0 ; time_quantum<=TQfinal ; time_quantum += TQinc) {
        simulate_job_mix(time_quantum);
    }

//  PRINT THE PROGRAM'S RESULT
    printf("best %i %i\n", optimal_time_quantum, total_process_completion_time);

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4

