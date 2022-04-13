#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>  
#include <linux/sched.h>
#include <sched.h>
#include <unistd.h>
#include <syscall.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <signal.h>

#define INVAL_INP	10
#define DEFAULT_STACK_SIZE	32768
#define THREAD_RUNNING 20
#define THREAD_TERMINATED 21
#define THREAD_RUNNABLE 22
#define NO_THREAD_FOUND 22
#define GUARD_PAGE_SIZE	4096
#define ALARM_TIME 100000  // in microseconds 
#define DEFAULT_SIGNAL_ARRAY_LENGTH 10


typedef unsigned long int thread_id;
typedef unsigned long int mThread;


typedef struct wrap_fun_info {
	void (*fun)(void *);
	void *args;
	mThread *thread;
} wrap_fun_info;


typedef struct signal_info {
    int* arr;
    int arr_size;
    int rem_sig_cnt;
} signal_info;

typedef struct node {
	thread_id tid;
	int stack_size;
	void *stack_start;
	wrap_fun_info* wrapper_fun;
    signal_info* sig_info;
	int state;
	void* ret_val;
    jmp_buf *t_context;      // use to store thread specific context
    struct node* next;
} node;

typedef node* node_list;

void scheduler();


// global tid table to store thread ids 
// of current running therads
node_list thread_list = NULL;

node scheduler_node;
node *curr_running_proc = NULL;


// Code reference : https://stackoverflow.com/questions/69148708/alternative-to-mangling-jmp-buf-in-c-for-a-context-switch
long int mangle(long int p) {
    long int ret;
    asm(" mov %1, %%rax;\n"
        " xor %%fs:0x30, %%rax;"
        " rol $0x11, %%rax;"
        " mov %%rax, %0;"
        : "=r"(ret)
        : "r"(p)
        : "%rax"
    );
    return ret;
}

void traverse() {
    node *nn = thread_list;
    while(nn) {
        printf("%d %d   ", nn->state, nn->stack_size);
        nn = nn->next;
    }
    printf("\n");
}


void enable_alarm_signal() {
    sigset_t signalList;
    sigfillset(&signalList);
    sigaddset(&signalList, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &signalList, NULL);
}

void handle_pending_signals()
{
    if (!curr_running_proc)
        return;
    ualarm(0,0);
    int k = curr_running_proc->sig_info->rem_sig_cnt;
    sigset_t signal_list;
    for (int i = 0; i < k; i++)
    {
        sigaddset(&signal_list, curr_running_proc->sig_info->arr[i]);
        sigprocmask(SIG_UNBLOCK, &signal_list, NULL);
        printf("ss = %d\n",  curr_running_proc->sig_info->arr[curr_running_proc->sig_info->rem_sig_cnt - 1]);
        curr_running_proc->sig_info->rem_sig_cnt--;
        // printf("ps = %d\n", curr_running_proc->sig_info->rem_sig_cnt);
        raise(curr_running_proc->sig_info->arr[curr_running_proc->sig_info->rem_sig_cnt]);
        // kill(getpid(), --curr_running_proc->sig_info->arr[curr_running_proc->sig_info->rem_sig_cnt]);
    }
    ualarm(ALARM_TIME,0);

    enable_alarm_signal();
    printf("kk %d\n",  curr_running_proc->sig_info->rem_sig_cnt);
}

//only for testing;
void signal_handler_vtalarm() {
    printf("signal_handler_vtalarm %ld\n", curr_running_proc->tid);
    return;
}

// setjump returns 0 for the first time, next time it returns value used in longjump(here 2) 
// so switch to scheduler will execute only once.
void signal_handler_alarm() {
    // printf("inside signal handler\n");    
    // disable alarm
    ualarm(0,0);

    // printf("printing signals \n");
    // int k = curr_running_proc->sig_info->rem_sig_cnt;
    // for(int i=0; i<k; i++){
    //     printf("sig = %d\n", curr_running_proc->sig_info->arr[i]);
    // }

    // switch context to scheduler
    int value = sigsetjmp(*(curr_running_proc->t_context), 1);
    if(! value) {
        siglongjmp(*(scheduler_node.t_context), 2);
    }
    handle_pending_signals();
    return;
}

int execute_me() {
	// printf("inside execute me\n");
	node *nn = curr_running_proc;
	nn->state = THREAD_RUNNING;
	nn->wrapper_fun->fun(nn->wrapper_fun->args);
    printf("execute me end\n");
	nn->state = THREAD_TERMINATED;
	// printf("termination done\n");
    //TODO: IMP: don't call scheduler() directly,instead use long jump
    siglongjmp(*(scheduler_node.t_context), 2);

	return 0;
}


void scheduler() {
    while(1) {
        // printf("inside scheduler\n");
        if(curr_running_proc->state == THREAD_RUNNING)
            curr_running_proc->state = THREAD_RUNNABLE;
            
        // point next_proc to next thread of currently running process
        node *next_proc = curr_running_proc->next;
        if(! next_proc) next_proc = thread_list;

        while(next_proc->state != THREAD_RUNNABLE) {
            if(next_proc->next) next_proc = next_proc->next;
            else next_proc = thread_list;
        }

        curr_running_proc = next_proc;

        next_proc->state = THREAD_RUNNING;
        enable_alarm_signal();
        ualarm(ALARM_TIME, 0);
        siglongjmp(*(next_proc->t_context), 2);
    }
    
}

// insert thread_id node in beginning of list
void thread_insert(node* nn) {
	nn->next = thread_list;
	thread_list = nn;
}

 
void init_many_one() {
    
    scheduler_node.t_context = (jmp_buf*)malloc(sizeof(jmp_buf));

    scheduler_node.stack_start = mmap(NULL, GUARD_PAGE_SIZE + DEFAULT_STACK_SIZE , PROT_READ|PROT_WRITE,MAP_STACK|MAP_ANONYMOUS|MAP_PRIVATE, -1 , 0);
	mprotect(scheduler_node.stack_start, GUARD_PAGE_SIZE, PROT_NONE);
    scheduler_node.stack_size = DEFAULT_STACK_SIZE;
    scheduler_node.wrapper_fun = (wrap_fun_info*)malloc(sizeof(wrap_fun_info));
    scheduler_node.wrapper_fun->fun = scheduler;
    scheduler_node.wrapper_fun->args = NULL;

    (*(scheduler_node.t_context))->__jmpbuf[6] = mangle((long int)scheduler_node.stack_start+DEFAULT_STACK_SIZE+GUARD_PAGE_SIZE );
    (*(scheduler_node.t_context))->__jmpbuf[7] = mangle((long int)scheduler_node.wrapper_fun->fun);

    node *main_fun_node = (node *)malloc(sizeof(node));
    main_fun_node->state = THREAD_RUNNING;
    main_fun_node->tid = 0;
    main_fun_node->t_context = (jmp_buf*) malloc(sizeof(jmp_buf));
    main_fun_node->ret_val = 0;         // not required
    main_fun_node->stack_start = NULL;  // not required
    main_fun_node->stack_size = 0;      // not required
    main_fun_node->wrapper_fun = NULL;  // not required

    main_fun_node->sig_info = (signal_info*)malloc(sizeof(signal_info));
    main_fun_node->sig_info->arr = (int*)malloc(sizeof(int) * DEFAULT_SIGNAL_ARRAY_LENGTH);
    main_fun_node->sig_info->arr_size = DEFAULT_SIGNAL_ARRAY_LENGTH;
    main_fun_node->sig_info->rem_sig_cnt = 0;

    curr_running_proc = main_fun_node;
    thread_insert(main_fun_node);

    signal(SIGALRM, signal_handler_alarm);

    ualarm(ALARM_TIME, 0);
}

int thread_create(mThread *thread, void *attr, void *routine, void *args) {
    if(! thread || ! routine) return INVAL_INP;

    static thread_id id = 0;
    node *t_node = (node *)malloc(sizeof(node));
    t_node->tid = id++;
    *thread = t_node->tid;
    t_node->t_context = (jmp_buf*) malloc(sizeof(jmp_buf));
    t_node->ret_val = 0;         // not required
    t_node->stack_start = mmap(NULL, GUARD_PAGE_SIZE + DEFAULT_STACK_SIZE , PROT_READ|PROT_WRITE,MAP_STACK|MAP_ANONYMOUS|MAP_PRIVATE, -1 , 0);
	mprotect(t_node->stack_start, GUARD_PAGE_SIZE, PROT_NONE);
    t_node->stack_size = DEFAULT_STACK_SIZE;      // not required

    t_node->sig_info = (signal_info*)malloc(sizeof(signal_info));
    t_node->sig_info->arr = (int*)malloc(sizeof(int) * DEFAULT_SIGNAL_ARRAY_LENGTH);
    t_node->sig_info->arr_size = DEFAULT_SIGNAL_ARRAY_LENGTH;
    t_node->sig_info->rem_sig_cnt = 0;

    wrap_fun_info *info = (wrap_fun_info*)malloc(sizeof(wrap_fun_info));
	info->fun = routine;
	info->args = args;
	info->thread = thread;
    t_node->wrapper_fun = info;  // not required

    (*(t_node->t_context))->__jmpbuf[6] = mangle((long int)t_node->stack_start+DEFAULT_STACK_SIZE+GUARD_PAGE_SIZE );
    (*(t_node->t_context))->__jmpbuf[7] = mangle((long int)execute_me);
    
    t_node->state = THREAD_RUNNABLE;
    thread_insert(t_node);
}

int thread_join(mThread tid, void **retval) {
	if(! retval)
		return INVAL_INP;
	node* n = thread_list;

	while(n && n->tid != tid)
		n = n->next;

	if(!n)
		return NO_THREAD_FOUND;

	while(n->state != THREAD_TERMINATED)
		;

	*retval = n->ret_val;
	return 0;
}


void thread_kill(mThread thread, int signal){
    ualarm(0,0);
    if (signal == SIGINT || signal == SIGCONT || signal == SIGSTOP)
        kill(getpid(), signal);
    else
    {
        if(curr_running_proc->tid == thread)
            raise(signal);
        else{
            node* n = thread_list;

            while(n && n->tid != thread){
                n = n->next;
                if(n==NULL)
                    return;
            }
            // if((n->sig_info->rem_sig_cnt == n->sig_info->arr_size))
            //     n->sig_info->arr = realloc(n->sig_info->arr, 2*n->sig_info->arr_size);
            n->sig_info->arr[n->sig_info->rem_sig_cnt++] = signal;
            printf("inside thread kill %d %d\n", n->sig_info->arr[n->sig_info->rem_sig_cnt - 1], signal);
        }
    }
    ualarm(ALARM_TIME, 0);
}


void thread_exit(void *retval) {
	node* n = curr_running_proc;
	n->ret_val = retval;
	n->state = THREAD_TERMINATED;
    siglongjmp(*(scheduler_node.t_context), 2);

	// syscall(SYS_exit, EXIT_SUCCESS);
}

void f1() {
	while(1){
        sleep(1);
	    printf("inside 1st fun.\n");
    }
}

void f2() {
    while(1){
        sleep(1);
	    printf("inside 2nd fun.\n");
    }
}

void f3() {
    int count = 0;
    void *a;
    while(1){
	    printf("inside 3rd function\n");
        sleep(1);
        count+=1;
        if(count > 4)
            thread_exit(a);
    }
}

int main() {
    mThread td;
	mThread tt, tm;

	init_many_one();

	thread_create(&td, NULL, f1, NULL);
    thread_create(&tt, NULL, f2, NULL);
    thread_create(&tm, NULL, f3, NULL);
    signal(SIGVTALRM, signal_handler_vtalarm);
    printf("sending signal to %ld\n", tt);
    thread_kill(tt, SIGVTALRM);

	
    node* t = thread_list;
    
    void **a;
    // printf("%ld\n", tm);
    // exit(1);
    // thread_join(tm, a);
    printf("join success");
    // return 0;
    // sleep(1);
    while(1){
        sleep(1);
	    printf("inside main fun.\n");
    }
    return 0;
}
