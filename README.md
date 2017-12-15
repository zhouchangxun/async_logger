# async_logger

## usage
```c
#include "async_logger.h" 

void test(){
  LOG_INFO("log in function");
}
int main()
{
  async_logger_init("app1.log", LOG_LVL_INFO, LOG_TARGET_CONSOLE|LOG_TARGET_FILE); //init and set log level.
  LOG_WARN("this is a  warn:%d",520);
  LOG_INFO("this is a  info:%s %d","age", 26);
  LOG_DEBUG("this is a  debug:%d",520);  //this will not output.

  test();
  return 0;
}
```
console and log file output as followed:
```c
2017/11/6 14:0:16 [INFO][24573][async_logger.c:295]async_logger_init(): initialize async logger ...
2017/11/6 14:0:16 [WARN][24573][test.c:10]main(): this is a  warn:520
2017/11/6 14:0:16 [INFO][24573][test.c:11]main(): this is a  info:hehe
2017/11/6 14:0:16 [INFO][24573][test.c:5]test(): log in function
2017/11/6 14:0:16 [INFO][24573][async_logger.c:265]async_logger_atexit(): destroy async logger ...
```
## todo
- colorful output.
- suport multi target device.

# contact 
 changxunzhou@qq.com
