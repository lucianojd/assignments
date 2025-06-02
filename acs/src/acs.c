// TODO
// 1. refactor to be more legible
// 2. move queue, customer, and time related functions to acs.h [x]
// 3. implement queue and customer list with dynamic memory
// 4. make all parameters configurable via arguments

#include <semaphore.h>
#include <unistd.h>
#include <stdarg.h>
#include <getopt.h>

#include "acs.h"

// Timing and logging.
struct timespec global_start_time;

void log_q(const char*, Queue*, pthread_mutex_t*);
void log_rt(const char*, Customer);

static int real_time = 0;
static int ms = 0;

static struct option options[] = {
	{"clerk_num", required_argument, NULL, 'c'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{"real_time", no_argument, &real_time, 1},
	{"ms", no_argument, &ms, 1},
	{NULL, 0, NULL, 0}
};

void read_customers(const char* filename);

// Thread routines.
void init(int, char**);
void start();
void stop();
void* customer_tr(void*);
void* clerk_tr(void*);
void* log_tr(void*);

// Clerk related variables.
unsigned int clerk_num = DEFAULT_CLERK_NUM;
unsigned int class_num = DEFAULT_CLASS_NUM;
int* winner = NULL;
int* clerk_q = NULL;
sem_t* clerk_sem = NULL;
pthread_mutex_t* clerk_q_mut = NULL;

// Customer related variables.
Customer *customers = NULL;
unsigned int customer_num = 0;

Queue** service_q = NULL; // Store the customers being served.
pthread_cond_t* service_cnd_var = NULL; // Conditional variable for the service queue.
pthread_mutex_t* service_q_mut = NULL; // Mutex for the service queues.

pthread_mutex_t arriving_mut;
Queue *arriving = NULL;

pthread_mutex_t leaving_mut;
Queue *leaving = NULL;

int main(int argc, char *argv[]) {
	init(argc, argv);

	start();

	return 0;
}

void start()
{
	int id[clerk_num];
	pthread_t clerk_thread_id[clerk_num];

	for(int i = 0; i < clerk_num; i++)
	{
		id[i] = i;
		pthread_create(&clerk_thread_id[i], NULL, clerk_tr, (void*) &id[i]);
	}

	for(int i = 0; i < customer_num; i++)
		pthread_create(&customers[i].pthread_id, NULL, customer_tr, (void*) &customers[i]);

	pthread_t log_thread_id = 0;
	if(!real_time)
		pthread_create(&log_thread_id, NULL, log_tr, NULL);

	for(int i = 0; i < customer_num; i++)
		pthread_join(customers[i].pthread_id, NULL);

	if(!real_time)
		pthread_cancel(log_thread_id);

	for(int i = 0; i < clerk_num; i++)
		pthread_cancel(clerk_thread_id[i]);
}

void stop()
{
	// Clean up arrays.
	free(winner);
	free(clerk_q);

	// Clean up queues.
	destroy(arriving);
	free(arriving);

	destroy(leaving);
	free(leaving);

	for(int i = 0; i < class_num; i++)
	{
		destroy(service_q[i]);
		free(service_q[i]);
	}

	free(clerk_sem);
	free(clerk_q_mut);
	free(service_cnd_var);
	free(service_q_mut);
}

void init(int argc, char** argv)
{
	if(argc < 2)
	{
		fprintf(stderr, "acs [OPTION]... [FILE]\n");
		exit(ERR_ARGS);
	}

	int c = 0;
	int opt_index = 0;

	for(;c >= 0;)
	{
		switch (c = getopt_long(argc, argv, "c:mhvr", options, &opt_index))
		{
		case 'c':
			clerk_num = atoi(optarg);
			break;
		case 'h':
			fprintf(stderr, "acs [OPTION]... [FILE]\n");
			fprintf(stderr, "  -c, --clerk_num=NUM     Number of clerks (default: %d)\n", DEFAULT_CLERK_NUM);
			fprintf(stderr, "  -m, --ms                Change unit of operations to ms to speed up the simulation\n");
			fprintf(stderr, "  -r, --real_time         Switch to real-time logging\n");
			fprintf(stderr, "  -h, --help              Display this help message\n");
			fprintf(stderr, "  -v, --version           Display version information\n");
			exit(0);
			break;
		case 'v':
			fprintf(stderr, "acs version %d.%d.%d\n", acs_VERSION_MAJOR, acs_VERSION_MINOR, acs_VERSION_PATCH);
			exit(0);
			break;
		case '?':
			fprintf(stderr, "acs: invalid option -- '%c'\n", optopt);
			fprintf(stderr, "Try 'acs --help' for more information.\n");
			exit(ERR_ARGS);
			break;
		default:
			break;
		}
	}

	read_customers(argv[argc - 1]);

	// Allocate memory for the service queues.
	service_q = (Queue**) malloc(sizeof(Queue*) * class_num);
	alloc_err(service_q, "init", "creating service queues");

	service_q_mut = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t) * class_num);
	alloc_err(service_q_mut, "init", "creating service queue mutexes");

	service_cnd_var = (pthread_cond_t*) malloc(sizeof(pthread_cond_t) * class_num);
	alloc_err(service_cnd_var, "init", "creating service queue condition variables");

	for(int i = 0; i < class_num; i++)
	{
		service_q[i] = (Queue*) malloc(sizeof(Queue));
		alloc_err(service_q[i], "init", "creating service queue");

		pthread_cond_init(&service_cnd_var[i], NULL);
		pthread_mutex_init(&service_q_mut[i], NULL);
	}

	// Allocated memory for the clerk semaphores.
	clerk_sem = (sem_t*) malloc(sizeof(sem_t) * clerk_num);
	alloc_err(clerk_sem, "init", "creating clerk semaphores");

	for(int i = 0; i < clerk_num; i++)
	{
		sem_init(&clerk_sem[i], 0, 0);
	}

	clerk_q = (int*) malloc(sizeof(int) * class_num);
	alloc_err(clerk_q, "init", "creating clerk queue");

	winner = (int*) malloc(sizeof(int) * class_num);
	alloc_err(winner, "init", "creating winner array");

	clerk_q_mut = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t) * class_num);
	alloc_err(clerk_q_mut, "init", "creating clerk mutexes");

	for(int i = 0; i < class_num; i++)
	{
		pthread_mutex_init(&clerk_q_mut[i], NULL);

		winner[i] = FALSE;
		clerk_q[i] = 0;	
	}

	// Allocate memory for arrival array.
	pthread_mutex_init(&arriving_mut, NULL);
	arriving = (Queue*) malloc(sizeof(Queue));
	alloc_err(arriving, "init", "creating arriving queue");
	arriving->head = NULL;
	arriving->len = 0;


	// Allocate memory for leaving array.
	pthread_mutex_init(&leaving_mut, NULL);
	leaving = (Queue*) malloc(sizeof(Queue));
	alloc_err(leaving, "init", "creating leaving queue");
	leaving->head = NULL;
	leaving->len = 0;

	// Capture start time.
	clock_gettime(CLOCK_MONOTONIC, &global_start_time);
}

void log_q(const char* msg, Queue* q, pthread_mutex_t* q_mut)
{
	char* str = NULL;
	int len = 0;

	pthread_mutex_lock(q_mut);
	len = q->len;
	customer_q_to_str(q, q->len, &str);
	pthread_mutex_unlock(q_mut);

	printf("%s(%d): %s\n", msg, len, str);

	free(str);
}

void log_rt(const char* msg, Customer customer)
{
	char *str = NULL;
	customer_to_string(customer, &str);

	long time = ms ? elapsed_us(&global_start_time) : elapsed_ms(&global_start_time);
	char* unit = ms ? "ms" : "s";

	printf("[%.2f%s]: %s %s\n", time / 1000.0, unit, str, msg);
	free(str);
}

void *log_tr(void*)
{
	printf(CLEAR_SCREEN);
	printf(RESET_CURSOR);

	long time = 0;
	char* unit = ms ? "ms" : "s";

	for(;;)
	{
		time = ms ? elapsed_us(&global_start_time) : elapsed_ms(&global_start_time);
		printf("[%.2f%s]\n", time / 1000.0, unit);

		log_q("arriving", arriving, &arriving_mut);

		char* str = malloc(sizeof(char) * DEFAULT_STRING_BUFFER_SIZE);
		for(int i = 0; i < class_num; i++)
		{
			snprintf(str, DEFAULT_STRING_BUFFER_SIZE, "serviceq[%d]", i);
			log_q(str, service_q[i], &service_q_mut[i]);
		}
		free(str);

		log_q("leaving", leaving, &leaving_mut);

		printf("\n");

		usleep(ms ? MS_TO_US(DEFAULT_LOG_RATE) : S_TO_US(DEFAULT_LOG_RATE));
	}
}

void *customer_tr(void *customer_input)
{
	Customer *info = (Customer*)customer_input;
	int my_id    = info->id;
	int my_class = info->cls;
	int my_clerk = 0;

	// The customer arrives after arrival_t seconds.
	// Customer is added to the arriving list.
	
	if(real_time)
		log_rt("arrived", *info);

	pthread_mutex_lock(&arriving_mut);
	add(arriving, *info);
	pthread_mutex_unlock(&arriving_mut);

	usleep(ms ? MS_TO_US(info->arrival_t) : S_TO_US(info->arrival_t));

	pthread_mutex_lock(&arriving_mut);
	del(arriving, my_id);
	pthread_mutex_unlock(&arriving_mut);

	//Enter service queue.
	pthread_mutex_lock(&service_q_mut[my_class]);
	add(service_q[my_class], *info);
	
	//Wait in the queue.
	for(;;)
	{
		if(real_time)
			log_rt("waiting in queue", *info);

		pthread_cond_wait(&service_cnd_var[my_class], &service_q_mut[my_class]);
		if(peek_id(service_q[my_class]) == my_id && !winner[my_class])
		{
			rem(service_q[my_class]);
			winner[my_class] = TRUE;
			my_clerk = clerk_q[my_class];
			break;
		}
	}
	pthread_mutex_unlock(&service_q_mut[my_class]);
	
	//Let other threads finish responding to broadcast.
	usleep(BROADCAST_WAIT);
	
	info->waiting_t = 0;

	//Begin serving process.
	if(real_time)
		log_rt("being served", *info);

	sem_post(&clerk_sem[my_clerk]);       					//Signal clerk you're being served by.
	usleep(ms ? MS_TO_US(info->service_t) : S_TO_US(info->service_t));	//Service.
	sem_post(&clerk_sem[my_clerk]);						//Signal clerk that service has finished.

	if(real_time)
		log_rt("finished being served", *info);

	pthread_mutex_lock(&leaving_mut);
	add(leaving, *info);
	pthread_mutex_unlock(&leaving_mut);

	if(real_time)
		log_rt("left", *info);

	pthread_exit(NULL);
}

void *clerk_tr(void *clerk_info) 
{
	int my_id = *((int*)clerk_info); //My id.
	int q_id = -1; 			 //Id of queue currently being worked on.

	for(;;)
	{
		//Wait for a bit while queues are being used.
		usleep(CLERK_WAIT);

        	//Check the business queue first.
		for(int i = class_num - 1; i >= 0; i--)
		{
		    //This clerk mutex is to prevent a clerk grabbing
		    //the q_mutex[] while customers respond to the broadcast.
			if(!pthread_mutex_trylock(&clerk_q_mut[i]))
			{
				if(!pthread_mutex_trylock(&service_q_mut[i]))
				{
					if(service_q[i]->len > 0)
					{
						q_id = i;
						break;
					}
					pthread_mutex_unlock(&service_q_mut[i]);
				}
				pthread_mutex_unlock(&clerk_q_mut[i]);
			}
		}

		if(q_id >= 0)
		{
			clerk_q[q_id] = my_id;				//Set clerk id value so customer can receive it.
			winner[q_id] = FALSE;				//Reset winner value.
			pthread_cond_broadcast(&service_cnd_var[q_id]);	//Wake up customers.
			pthread_mutex_unlock(&service_q_mut[q_id]);	//Unlock the mutex.
			sem_wait(&clerk_sem[my_id]);			//Wait for cutomer to start being served.
			pthread_mutex_unlock(&clerk_q_mut[q_id]);	//Release clerk mutex.
			sem_wait(&clerk_sem[my_id]);			//Wait for signal that service is finished from customer.
            		q_id = -1;
		}
				
	}
		
	pthread_exit(NULL);
}

void read_customers(const char *filename)
{
	FILE *f = fopen(filename, "r");

	file_err(f, "read_customers", filename);

	char *line = NULL;
	size_t len = 0;

	int count = 0;
	int customer_max = DEFAULT_CUSTOMER_NUM;

	customers = (Customer*) malloc(sizeof(Customer) * customer_max);

	alloc_err(customers, "read_customers", "reading customers");

	for(;getline(&line, &len, f) != -1;)
	{
		int id = 0;
		int cls = 0;
		int arrival_t = 0;
		int service_t = 0;

		sscanf(line, "%d:%d,%d,%d\n", &id, &cls, &arrival_t, &service_t);
		customers[count] = (Customer){id, cls, arrival_t, service_t, 0.0, 0}; 
		count++;

		if(count >= customer_max)
		{
			customer_max *= 2;
			customers = (Customer*) realloc(customers, sizeof(Customer) * customer_max);
			alloc_err(customers, "read_customers", "reading customers");
		}
	}

	customer_num = count;
	customers = (Customer*) realloc(customers, sizeof(Customer) * customer_num);
}
