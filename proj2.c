// login: xholinp00
// title: IOS-project2

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>

FILE *out;

// Shared data structure
// Contains semaphores and shared variables
// that are used for synchronization and communication
// between processes
typedef struct {
    sem_t write_sem;      
    sem_t truck_board0;    
    sem_t car_board0;
    sem_t truck_board1;    
    sem_t car_board1;      
    sem_t board_done;     
    sem_t truck_unload;   
    sem_t car_unload;     
    sem_t unload_done;    
    int actionCounter;
    int cars_waiting0;    
    int trucks_waiting0;  
    int cars_waiting1;    
    int trucks_waiting1;  
    int cars_on_board;
    int trucks_on_board;
    sem_t mutex;                // i ended up splitting the mutex into two
    sem_t mutex2;               // one for ferry and one for vehicles
} shared_data;

// Function that handle printing, semaphores and actionCounter increment
void my_printf(shared_data *sh, const char *format, ...) {
    va_list args;
    va_start(args, format);                 
    sem_wait(&sh->write_sem);                   
    fprintf(out, "%d: ", sh->actionCounter);    
    sh->actionCounter++;                        
    vfprintf(out, format, args);            
    fflush(out);                                
    sem_post(&sh->write_sem);                   
    va_end(args);
}

// this helps to easily clean up after ypurself
// it closes the file, destroys semaphores and unmaps shared memory
// only call when file is open, semaphores are initialized and shared memory is allocated
void cleanup(shared_data *sh) {
    sem_destroy(&sh->write_sem);
    sem_destroy(&sh->truck_board0);
    sem_destroy(&sh->car_board0);
    sem_destroy(&sh->truck_board1);
    sem_destroy(&sh->car_board1);
    sem_destroy(&sh->board_done);
    sem_destroy(&sh->truck_unload);
    sem_destroy(&sh->car_unload);
    sem_destroy(&sh->unload_done);
    sem_destroy(&sh->mutex);
    sem_destroy(&sh->mutex2);

    munmap(sh, sizeof(shared_data));
    fclose(out);
}

// fucction to help sleep for a random time
// in interval <0, max_ms> with usleep and random
// (getpid() << 16) should be used to get different seed
// for each process 
void wait_random_time(int max_ms) {
    srandom(time(NULL) ^ (getpid() << 16)); // Seed the random number generator
    int delay = random() % (max_ms + 1);    // Generate a random delay
    usleep(delay * 1000);                   // Convert to microseconds
}

// function that handles the truck process
// it handles waiting, changing shared data, semaphores for
// boarding, unloading and printing
void truck_process(shared_data *sh, int idN, int idP, int max_wait) {
    // start print
    int P2=(idP+1)%2;                                   
    my_printf(sh, "N %i: started\n", idN);
    wait_random_time(max_wait);                         
    my_printf(sh, "N %i: arrived to %i\n", idN, idP);
    
    // variable update
    sem_wait(&sh->mutex2);                                  
    if(idP == 0) sh->trucks_waiting0++;                 
    else sh->trucks_waiting1++;
    sem_post(&sh->mutex2);

    // wait for the boarding semaphore on coresponding side
    if(idP == 0) sem_wait(&sh->truck_board0);                     
    else sem_wait(&sh->truck_board1);
    
    // boarding print and semaphore release
    my_printf(sh, "N %i: boarding\n", idN);
    sem_post(&sh->board_done);
    
    sem_wait(&sh->mutex2);                      
    sh->trucks_on_board++;                           
    sem_post(&sh->mutex2);                      

    sem_wait(&sh->truck_unload);                        
    my_printf(sh, "N %i: leaving in %i\n", idN, P2);    
    sem_post(&sh->unload_done);                         
    exit(0);                                            
}


// same as above, but for cars :o
void car_process(shared_data *sh, int idO, int idP, int max_wait) {
    int P2=(idP+1)%2;                                   
    my_printf(sh, "O %i: started\n", idO);
    wait_random_time(max_wait);                         
    my_printf(sh, "O %i: arrived to %i\n", idO, idP);

    sem_wait(&sh->mutex2);                                  
    if(idP == 0) sh->cars_waiting0++;                 
    else sh->cars_waiting1++;
    sem_post(&sh->mutex2);

    if(idP == 0) sem_wait(&sh->car_board0);
    else sem_wait(&sh->car_board1);
    

    my_printf(sh, "O %i: boarding\n", idO);
    sem_post(&sh->board_done);                          

    sem_wait(&sh->mutex2);
    sh->cars_on_board++;                                
    sem_post(&sh->mutex2);

    sem_wait(&sh->car_unload);                          
    my_printf(sh, "O %i: leaving in %i\n", idO, P2);
    sem_post(&sh->unload_done);                         

    exit(0);                                            

}

// this one handles the ferry process
// it handles waiting, changing shared data, semaphores for
// boarding, unloading and printing
// it also handles the ferry side switching
// and the vehicle type switching
void ferry_process(shared_data *sh, int N, int O, int capacity, int max_wait) {
    my_printf(sh, "P: started\n");
    int remaining_capacity = capacity;  
    int remaining_trucks = N;           
    int remaining_cars = O;             

    int PR = 0;                         // Current side of the ferry (0 or 1)
    int whos_lodin = 0;                 // 0 for cars, 1 for trucks
    wait_random_time(max_wait);         
    my_printf(sh, "P: arrived to %i\n", PR);


    // MAIN FERRY LOOP
    // The ferry will keep boarding and unloading vehicles until all vehicles are loaded and unloaded
    while (remaining_trucks > 0 || remaining_cars > 0 || sh->cars_on_board > 0 || sh->trucks_on_board > 0) {

        // UNLOADING
        while (sh->cars_on_board > 0 || sh->trucks_on_board > 0) {
            if (sh->trucks_on_board > 0) {
                sem_post(&sh->truck_unload);
                sem_wait(&sh->unload_done);

                sem_wait(&sh->mutex);
                sh->trucks_on_board--;
                sem_post(&sh->mutex);
            } 
            else if (sh->cars_on_board > 0) {
                sem_post(&sh->car_unload);
                sem_wait(&sh->unload_done);

                sem_wait(&sh->mutex);
                sh->cars_on_board--;
                sem_post(&sh->mutex);
            }
        }

        // BOARDING
        while(remaining_capacity > 0){
            // Check if there are any vehicles waiting on the current side
            sem_wait(&sh->mutex);
            int cars_available = (PR == 0) ? sh->cars_waiting0 : sh->cars_waiting1;
            int trucks_available = (PR == 0) ? sh->trucks_waiting0 : sh->trucks_waiting1;
            sem_post(&sh->mutex);

            // If no vehicles are waiting, break the loop
            if(cars_available == 0 && trucks_available == 0) {
                break;
            }
            else if(cars_available == 0 && remaining_capacity < 3){
                break;
            }

            // CAR BOARDING
            if(whos_lodin == 0 && remaining_capacity >= 1 && cars_available > 0){
                if(PR == 0) sem_post(&sh->car_board0);
                else sem_post(&sh->car_board1);               

                sem_wait(&sh->board_done);               
                remaining_cars--;                       
                remaining_capacity--;
                
                sem_wait(&sh->mutex);
                if (PR == 0) sh->cars_waiting0--;
                else sh->cars_waiting1--;
                sem_post(&sh->mutex);
            }

            // TRUCK BOARDING
            else if(whos_lodin == 1 && remaining_capacity >= 3 && trucks_available > 0){
                if (PR == 0) sem_post(&sh->truck_board0);
                else sem_post(&sh->truck_board1);
                
                sem_wait(&sh->board_done);
                remaining_trucks--;
                remaining_capacity -= 3;
                
                sem_wait(&sh->mutex);
                if (PR == 0) sh->trucks_waiting0--;
                else sh->trucks_waiting1--;
                sem_post(&sh->mutex);   
            }
            
            // Switch vehichele type
            whos_lodin = (whos_lodin + 1) % 2;      

            // If theres no space for trucks, switch to cars
            if(whos_lodin == 1 && remaining_capacity < 3){     
                whos_lodin = 0;                                 
            }

        }
        // Getting information about number of vehicles waiting on the current side
        // Only switch sides if:
        // 1. Ferry is full, or
        // 2. No more vehicles waiting on this side, or
        // 3. Only trucks are waiting but we don't have enough capacity
        sem_wait(&sh->mutex);
        int vehicles_waiting_current = (PR == 0) ? 
        (sh->cars_waiting0 + sh->trucks_waiting0) : 
        (sh->cars_waiting1 + sh->trucks_waiting1);
        int cars_waiting_current = (PR == 0) ? sh->cars_waiting0 : sh->cars_waiting1;
        sem_post(&sh->mutex);
        
        if(remaining_capacity == 0 || vehicles_waiting_current == 0 || (cars_waiting_current == 0 && remaining_capacity < 3)){ 
            my_printf(sh, "P: leaving %i\n", PR);
            if(remaining_capacity == capacity && remaining_cars == 0 && remaining_trucks == 0){
                break;
            }
            wait_random_time(max_wait);     
            PR = (PR + 1) % 2;                             
            my_printf(sh, "P: arrived to %i\n", PR);
            remaining_capacity = capacity; 
        }
    }

    my_printf(sh, "P: finish\n");
    exit(0); // Exit the ferry process
}

// Main function (:O)
int main(int argc, char *argv[]) {

    // Error handling for arguments count
    if (argc != 6) {
        fprintf(stderr, "Error: Invalid number of arguments\n");
        return 1;
    }

    // Convert command line arguments to integers
    int N = atoi(argv[1]);                  // Number of trucks (0 < N < 10000)
    int O = atoi(argv[2]);                  // Number of cars (0 < O < 10000)
    int capacity = atoi(argv[3]);           // (3 <= capacity <= 100)
    int car_max_wait = atoi(argv[4]);       // (0 < car_max_wait < 10000)
    int ferry_max_wait = atoi(argv[5]);     // (0 < ferry_max_wait < 1000)
    
    // Error handeling for arguments values
    if (N < 0 || N > 10000 || O < 0 || O > 10000 || capacity < 3 || capacity > 100 ||
        car_max_wait < 0 || car_max_wait > 10000 || ferry_max_wait < 0 || ferry_max_wait > 1000){
        fprintf(stderr, "Invalid arguments\n");
        return 1;
    }

    // Create shared memory for the shared data structure
    // Use mmap to allocate shared memory
    shared_data *sh = mmap( NULL, sizeof(shared_data),
                            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
    if (sh == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Initialize semaphores and shared variables
    sem_init(&sh->write_sem,    1, 1);
    sem_init(&sh->mutex,        1, 1);
    sem_init(&sh->mutex2,       1, 1);
    sem_init(&sh->truck_board0, 1, 0); 
    sem_init(&sh->car_board0,   1, 0);
    sem_init(&sh->truck_board1, 1, 0); 
    sem_init(&sh->car_board1,   1, 0);  
    sem_init(&sh->board_done,   1, 0);
    sem_init(&sh->truck_unload, 1, 0);
    sem_init(&sh->car_unload,   1, 0);
    sem_init(&sh->unload_done,  1, 0);
    
    sh->actionCounter = 1;     
    sh->cars_waiting0 = 0;     
    sh->trucks_waiting0 = 0;   
    sh->cars_waiting1 = 0;     
    sh->trucks_waiting1 = 0;   
    sh->cars_on_board = 0;      
    sh->trucks_on_board = 0;
    
    // Open the output file
    // and check if it was opened successfully
    out = fopen("proj2.out", "w");
    if (out == NULL) {
        fprintf(stderr, "Error: Cannot open output file\n");
        munmap(sh, sizeof(shared_data));
        return 1;
    }

    // Process for ferry
    pid_t pf = fork();
    if (pf < 0) { perror("fork"); exit(1); }
    if (pf == 0) {
        ferry_process(sh, N, O, capacity, ferry_max_wait);
    }

    // Processes for cars
    for (int idO = 1; idO <= O; idO++) {
        pid_t p = fork();
        if (p < 0) { perror("fork"); exit(1); }         // Error handling for fork
        if (p == 0) {                                   // child process
            srandom(time(NULL) ^ (getpid() << 16));     
            int side = random() % 2;                    // Randomly choose side (0 left or 1 right) 
            car_process(sh, idO, side, car_max_wait);
        }
    }

    // Procceses for trucks
    for (int idN = 1; idN <= N; idN++) {                // i hope i dont need
        pid_t p = fork();                               // to comment this again
        if (p < 0) { perror("fork"); exit(1); }         
        if (p == 0) {                                   
            srandom(time(NULL) ^ (getpid() << 16));     
            int side = random() % 2;                    
            truck_process(sh, idN, side, car_max_wait);
        }
    }
    
    // Wait for all child processes to finish
    while (wait(NULL) > 0);

    // Clean up semaphores, shared memory and close file
    cleanup(sh);
    return 0;
}
