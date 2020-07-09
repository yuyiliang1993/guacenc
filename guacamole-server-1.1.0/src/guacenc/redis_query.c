
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "cJSON.h"

#include <hiredis/hiredis.h>

#define log_stderr printf
#define log_stdout printf

redisContext *myredis_auth_connect(const char*ipaddr,int port,const char*password){
	redisContext *context = redisConnect(ipaddr,port);
	if(context == NULL || context->err){
		if(context){
			log_stderr("%s\n", context->errstr);
		}else{
			log_stderr("redisConnect error\n");
		}
		return NULL;
	}
	int err = 0;
	redisReply *reply = redisCommand(context, "auth %s",password);
	if (reply && reply->type == REDIS_REPLY_ERROR) {
		log_stderr("auth failed:%s",reply->str);
		err = 1;
	}
	freeReplyObject(reply);
	if(err){
		redisFree(context);
		context = NULL;
	}	
	return context;
}


int myredis_query(redisContext *context,const char *cmd){
	redisReply *reply = redisCommand(context,cmd);
	if(reply == NULL){
		log_stderr("reply==NULL");
		return 0;
	}
    int rv = 0;
	if(reply->type == REDIS_REPLY_ERROR){
		log_stderr("Failed execute command:%s\n",cmd);
		log_stderr("Error:%s\n",reply->str);
	}else{
		log_stdout("Successed command:\n%s",cmd);
        rv = 1;
	}
	freeReplyObject(reply);
    return rv;
}
