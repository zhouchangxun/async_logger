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

## todo
- colorful output.
- suport multi target device.

# contact 
 changxunzhou@qq.com
