// TODO add pid in the node to use same library instance for multiple processes.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>  
#include <linux/sched.h>
#include <sched.h>
#include <unistd.h>
 #include <syscall.h>
 #include <sys/mman.h>

#define INVAL_INP	10
#define DEFAULT_STACK_SIZE	32768
#define THREAD_RUNNING 20
#define THREAD_TERMINATED 21
#define NO_THREAD_FOUND 22
#define GUARD_PAGE_SIZE	4096

typedef unsigned long int thread_id;
typedef unsigned long int mThread;


typedef struct wrap_fun_info {
	void (*fun)(void *);
	void *args;
	mThread *thread;
} wrap_fun_info;

typedef struct node {
	thread_id tid;
	int stack_size;
	void *stack_start;
	struct node* next;
	wrap_fun_info* wrapper_fun;
	int state;
	void* ret_val;
} node;

typedef node* tid_list;


// global tid table to store thread ids 
// of current running therads
tid_list tid_table;


// insert thread_id node in beginning of list
int tid_insert(node* nn, thread_id tid, int stack_size, void *stack_start) {
	
	nn->tid = tid;
	nn->stack_size = stack_size;
	nn->stack_start = stack_start;
	nn->next = NULL;

	nn->next = tid_table;
	tid_table = nn;
}

void init_threading() {
	tid_table = NULL;
}

int execute_me(void *new_node) {
	node *nn = (node*)new_node;
	nn->state = THREAD_RUNNING;
	nn->wrapper_fun->fun(nn->wrapper_fun->args);
	nn->state = THREAD_TERMINATED;
	printf("termination done\n");
	return 0;
}

int thread_join(mThread tid, void **retval){
	if(!retval)
		return INVAL_INP;
	node* n = tid_table;

	while(n && n->tid!=tid)
		n = n->next;

	if(!n)
		return NO_THREAD_FOUND;

	while(n->state!=THREAD_TERMINATED)
		;

	*retval = n->ret_val;
	return 0;
}

void thread_exit(void *retval) {
	thread_id curr_tid = (thread_id)gettid();
	node *n = tid_table;

	while(n && n->tid != curr_tid)
		n = n->next;
	
	if(!n ) return;

	// free(n->wrapper_fun);
	// free(n->stack_start);
	n->ret_val = retval;
	n->state = THREAD_TERMINATED;
	syscall(SYS_exit, EXIT_SUCCESS);
}

int thread_kill(mThread thread, int signal) {
	int process_id = getpid();
	syscall(SYS_tgkill, process_id, thread, signal);
	return 0;
}

int thread_create(mThread *thread, void *attr, void *routine, void *args) {
	if(! thread || ! routine) return INVAL_INP;
	
	unsigned long int CLONE_FLAGS = CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD |CLONE_SYSVSEM|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID;
	wrap_fun_info *info = (wrap_fun_info*)malloc(sizeof(wrap_fun_info));
	info->fun = routine;
	info->args = args;
	info->thread = thread;
	
	void *stack = mmap(NULL, GUARD_PAGE_SIZE + DEFAULT_STACK_SIZE , PROT_READ|PROT_WRITE,MAP_STACK|MAP_ANONYMOUS|MAP_PRIVATE, -1 , 0);
	mprotect(stack, GUARD_PAGE_SIZE, PROT_NONE);

	node *new_node = (node*)malloc(sizeof(node));
	new_node->wrapper_fun = info;
	
	*thread = clone(execute_me, stack + DEFAULT_STACK_SIZE + GUARD_PAGE_SIZE, CLONE_FLAGS, (void *)new_node);	
	tid_insert(new_node,*thread, DEFAULT_STACK_SIZE, stack);
}

void myFun() {
	printf("inside 1st fun.\n");
	sleep(3);
	printf("below sleep\n");
}

void myF() {
	sleep(3);
	printf("inside 2nd fun\n");
}

int main() {
	mThread td;
	mThread tt;
	init_threading();
	thread_create(&td, NULL, myFun, NULL);
	sleep(1);
	// thread_kill(td, 12);	
	printf("hidd\n");
	sleep(5);
	printf("hidd\n");
	/*
	thread_create(&tt, NULL, myF, NULL);
	printf("bfr join.\n");
	void **a;
	thread_join(td, a);
	printf("maftr join\n");
	sleep(1);
	printf("bfr join1.\n");
	thread_join(tt, a);
	printf("maftr join2\n");
	//printf("%ld\n", tid_table->next->tid);
	*/
	/*
	node *tmp = tid_table;
	while(tmp) {
		printf("stack size %d\nabcd\n", tmp->stack_size);
		tmp = tmp->next;
	}*/
	return 0;
	}