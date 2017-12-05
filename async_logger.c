/*
 * Copyright (c) 2017 zhouchangxun(changxunzhou@qq.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

//include(system)
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
//include(custom)
#include "async_logger.h"

//config
#define LOG_NODE_POOL_SIZE     64
#define LOG_NODE_DATA_LEN      2048

//type
typedef enum {
    LOG_ITEM_ASYNC_WRITE,
    LOG_ITEM_ASYNC_EXIT
} item_types;

typedef enum {
    LOG_TARGET_CONSOLE=1,
    LOG_TARGET_FILE
}target_types;

typedef struct log_node
{
    item_types item_type;
    char data[LOG_NODE_DATA_LEN];
    int len;
    int priority;

    struct log_node *next;
}log_node;

struct log_queue{
    log_node pool[LOG_NODE_POOL_SIZE];
    log_node *head;
    log_node *tail;
    log_node *free;
};

struct logger {
  unsigned int drops;
  int level;
  int target;
  char * filename;
  struct log_queue queue;
};

#define LOG_NODE_NOWAIT        0
#define LOG_NODE_WAIT          1

static void console_out(char * msg);
static void file_out(char * msg);
//data
static pthread_t logger_thread;

static pthread_mutex_t log_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int log_queue_inited = 0;

static pthread_cond_t log_queue_cond;
static pthread_mutex_t log_queue_mutex;

static pthread_cond_t log_node_free_cond;
static pthread_mutex_t log_node_free_mutex;

static struct logger logger={
	.drops = 0,
        .level = LOG_LVL_INFO,
       	.queue= {
	  .head = NULL,
       	  .tail = NULL,
       	  .free = logger.queue.pool
	},
};


//method
void console_out(char * msg){
     printf("%s", msg);
}
void file_out(char * msg){
    FILE * fp;
    if((fp=fopen(logger.filename,"a"))==NULL)
    {
        printf("The file can not be opened.\n");
        return;
    }
    fputs(msg, fp);
    fclose(fp);
}
static void target_out(char * msg){
  if(logger.target & LOG_TARGET_CONSOLE){
    console_out(msg); 
  }
  if(logger.target & LOG_TARGET_FILE){
    file_out(msg); 
  }
}
static char * level_to_str(int lvl){
  switch(lvl){
	case LOG_LVL_ERR: return "ERR";
	case LOG_LVL_WARN: return "WARN";
	case LOG_LVL_INFO: return "INFO";
	case LOG_LVL_DEBUG: return "DEBUG";
	default: return "UNKOWN";
  }
}

static int append_time(char * buf)
{
    time_t t;
    struct tm * lt;
    time (&t);
    lt = localtime (&t);
    return sprintf (buf, "%d/%d/%d %d:%d:%d ", 
        lt->tm_year+1900, lt->tm_mon, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
}
static void
log_queue_run(void)
{
    struct log_node *node;

    for (;;) {
        pthread_mutex_lock(&log_queue_mutex);
        while (logger.queue.head == NULL) {
            pthread_cond_wait(&log_queue_cond, &log_queue_mutex);
        }
	//get a new msg from head.
	node = logger.queue.head;
	logger.queue.head = node->next;
        pthread_mutex_unlock(&log_queue_mutex);

        /* main work here */
        switch (node->item_type) {
            case LOG_ITEM_ASYNC_WRITE:
		target_out(node->data);
                break;

            case LOG_ITEM_ASYNC_EXIT:
                return; //thread exit normally.

            default:
                break;
        }

        /* put node into log_node_free's head */
        pthread_mutex_lock(&log_node_free_mutex);

        node->next = logger.queue.free;
        logger.queue.free = node;

        pthread_cond_signal(&log_node_free_cond);
        pthread_mutex_unlock(&log_node_free_mutex);
    }
}

static int
log_queue_init(void)
{
    int i;

    memset(logger.queue.pool, 0, sizeof(logger.queue.pool));
    for (i = 0; i < LOG_NODE_POOL_SIZE - 1; i++) {
        logger.queue.pool[i].next = &(logger.queue.pool[i + 1]);
    }
    logger.queue.pool[LOG_NODE_POOL_SIZE - 1].next = NULL;

    pthread_cond_init(&log_queue_cond, NULL);
    pthread_mutex_init(&log_queue_mutex, NULL);
    pthread_cond_init(&log_node_free_cond, NULL);
    pthread_mutex_init(&log_node_free_mutex, NULL);

    if (pthread_create(&logger_thread, NULL, (void *(*)(void *))&log_queue_run, NULL) != 0)
        return -1;

    return 0;
}

static struct log_node *
log_queue_get_free_item(int wait)
{
    struct log_node *node;

    pthread_mutex_lock(&log_node_free_mutex);
    while (logger.queue.free == NULL) {
        /* no free work items, return if no wait is requested */
        if (wait == 0) {
            logger.drops++;
            pthread_mutex_unlock(&log_node_free_mutex);
            return NULL;
        }
        pthread_cond_wait(&log_node_free_cond, &log_node_free_mutex);
    }


    /* move up log_node_free */
    node = logger.queue.free;
    logger.queue.free = logger.queue.free->next;

    pthread_mutex_unlock(&log_node_free_mutex);

    return node;
}

static void
log_queue_put_item(struct log_node *node)
{

    pthread_mutex_lock(&log_queue_mutex);

    node->next = NULL;
    if (logger.queue.head == NULL) {
        logger.queue.head = node;
        logger.queue.tail = node;
    } else {
        logger.queue.tail->next = node;
        logger.queue.tail = node;
    }

    /* notify worker thread */
    pthread_cond_signal(&log_queue_cond);

    pthread_mutex_unlock(&log_queue_mutex);
}

static void
async_logger_atexit(void)
{
    struct log_node *node;

    if (log_queue_inited == 0)
        return;

    /* Wait for the worker thread to exit */
    LOG_INFO("destroy async logger ...");
    node = log_queue_get_free_item(LOG_NODE_WAIT);
    node->item_type = LOG_ITEM_ASYNC_EXIT;
    log_queue_put_item(node);
    pthread_join(logger_thread, NULL);
}

int
async_logger_init(const char *filename, int level , int target)
{

    if(log_queue_inited) return 0;

    pthread_mutex_lock(&log_init_mutex);

    if (log_queue_inited == 0) {
        if (log_queue_init() != 0) {
            pthread_mutex_unlock(&log_init_mutex);
            return -1;
        }
    }
    log_queue_inited = 1;

    pthread_mutex_unlock(&log_init_mutex);

    logger.level = level;
    logger.filename = (char *)filename;
    logger.target = target;
    atexit(async_logger_atexit); //register exit callback.
    
    LOG_INFO("initialize async logger ...");
    return 0;
}

void
async_log_queue(int priority, const char *format, va_list ap)
{
    struct log_node *node;
    char *p;
    int s1, s2;

    node = log_queue_get_free_item(LOG_NODE_NOWAIT);
    if (node == NULL)
        return;

    p = node->data;
    s1 = sizeof(node->data);
    s2 = vsnprintf(p, s1, format, ap);
    if (s2 >= s1) {
        /* message was truncated */
        s2 = s1 - 1;
        p[s2] = '\0';
    }
    node->len = s2;
    node->priority = priority;
    node->item_type = LOG_ITEM_ASYNC_WRITE;
    log_queue_put_item(node);
}

void async_log(int lvl, const char * func, const char * file, int line, const char *format, ...)
{
       va_list ap ;
       char buf[520];
       int offset = 0;
       if(lvl > logger.level) return;

       va_start(ap, format);
       //printf format: %[flags] [width] [.precision] [{h | l | I64 | L}]type
       offset += append_time(buf+offset);
       offset += sprintf(buf+offset, "[%s]", level_to_str(lvl));
       offset += sprintf(buf+offset, "[%d][%s:%d]%s(): %s\n", getpid(), file, line, func, format);

       async_log_queue(lvl, buf, ap);

       va_end(ap); 

       return ;
}

