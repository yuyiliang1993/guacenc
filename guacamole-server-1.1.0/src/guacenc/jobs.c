#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <pthread.h>

#include "encode_video.h"
#include "cJSON.h"
#include  "guacenc.h"

#undef DELETE_FILE
#define DELETE_FILE unlink


struct Job{
    int fd;
    int recording_fd;
    pthread_t thid;
    char buffer[32768];//缓存数据，读取文件或者socket
    int length;//数据长度
    int buffer_size;
    int terminate;
    int (*handler)(void*arg);
    struct VideoConfig *config;
    InsTransVideo_t *ins;
    void *ptr;
    struct Job *next;
};


static int job_set_fd_nonblock(int fd){
    int flags = fcntl(fd,F_GETFL,0);
    flags |= O_NONBLOCK;
    return fcntl(fd,F_SETFL,flags);
}


int job_read_stream(int fd,InsTransVideo_t *ins){
    if(ins == NULL)
        return (-1);
    int loop = 1;
    int nbytes = 0;
    int rv = 1;
    while(loop){
        if(ins->offsetLen > sizeof(ins->data)){
            fprintf(stderr,"protocol data too long\n");
            ins->offsetLen = 0;
        }
        nbytes = read(fd,ins->data+ins->offsetLen,1);
        if(nbytes < 0){
            if(errno != EAGAIN)
                rv = -1;
            else 
                rv = 1;//非阻塞，按连接正常处理
            loop = 0;
        }
        else if(nbytes == 0){
            return 0;//断开连接
        }
        else if(nbytes > 0){
            ins->offsetLen += nbytes;
            if(ins->data[ins->offsetLen -1] == ';'){
                ins->data[ins->offsetLen] = 0;
                if(ins_parser_read(ins) < 0){
                    rv = -1;
                }

                ins->offsetLen = 0;
            }
        }
    }
    return rv;
}

int job_loop_select(void *arg){
    fd_set rfds;
    int max = 0;
    struct Job *job = (struct Job*)arg;
    if(job->fd > max)
        max = job->fd;

    if(job->ins == NULL)
        return -1;
    //设置fd非阻塞
    job_set_fd_nonblock(job->fd);
     
    while(1){
        
        FD_ZERO(&rfds);
        FD_SET(job->fd,&rfds);       
        int n  = select(max+1,&rfds,NULL,NULL,NULL);
        if(n <= 0){
            if(n == 0)
                continue; 
            if(errno == EINTR)
                continue;
            perror("select");
            break;
        }
         
        if(FD_ISSET(job->fd,&rfds)){
           if(job_read_stream(job->fd,job->ins) <= 0){
                return (-1);
           }
        }
       
    }    
    return 0;
}


int job_check_client_break(int fd){
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd,&rfds);  
    struct timeval t={
        .tv_sec = 0,
        .tv_usec = 100
    };
    
    memset(&t,0,sizeof(t));
    int rv = select(fd+1,&rfds,NULL,NULL,&t);
    if(rv > 0){
        char buf[512];
        int rv = read(fd,buf,512);
        if(rv == 0)
            return 1;
        else if(rv > 0){
            printf("client data:%s\n",buf);//can not output
        }
    }
    else if(rv < 0)
        return 1;
    
    return 0;
}


int job_loop_read_recodingfile(void *arg){

    int rv = 0;
  
    int loop = 1;

    int status_sem_hold = 0;
   // int cli_break_flag=0;

    long last_active;

   // long current_utc_time ;
   
    struct Job *job = (struct Job*)arg;
   
    InsTransVideo_t *ins = job->ins;

 
    server_inter_t *inter = (server_inter_t*)job->ptr;

    job->recording_fd = open(job->config->recordingfile,O_RDONLY);

    if(job->recording_fd < 0){
        printf("open:%s:%s\n",__func__,strerror(errno));
        return (-1);
    }
    
    sem_wait(&inter->sem);//获取执行型号量
    last_active = time(NULL);
    status_sem_hold = 1;
    printf("current time:%ld\n",time(NULL));

    int sched_flag = 0;
    while(loop){
       if( sched_flag || time(NULL) - last_active >= 5){ //5s
            printf("current time:%ld\n",time(NULL));
            sem_post(&inter->sem);//让别的线程执行
            status_sem_hold = 0;
            sched_yield();
            sem_wait(&inter->sem);//等待信号量执行
            last_active =  time(NULL);
            status_sem_hold = 1;
            sched_flag = 0;
       }
       
       if(job->length >= job->buffer_size)
           job->length = 0;
       
       rv = read(job->recording_fd,ins->data +ins->offsetLen,1);

       if(rv > 0){
            ins->offsetLen += rv;
            if(ins->data[ins->offsetLen-1] == ';'){
                if(ins_parser_read(ins) < 0){
                    printf("ins_parser_read error\n");
                    return -1;
                }
                ins->offsetLen = 0;//完整的指令执行之后需要清零               
            }
       }
       
       if(rv == 0){
            if(job->terminate){
                printf("Loop will break!\n");
                loop = 0;
            }else {
               sched_flag = 1; 
            }
       }
       
       if(rv < 0){
            printf("Read error:%s:%s\n",__func__,strerror(errno));
            break;
       }
    }
    if(status_sem_hold){
        sem_post(&inter->sem);  
    }
    return 0;
}



//struct VideoConfig *global_config = NULL;
//config:must malloc,free it by job_destory
static struct Job*job_create(struct VideoConfig*config,int fd){
    struct Job* job = (struct Job*)malloc(sizeof(struct Job));
    job->config = config;
    job->fd = fd;
    job->recording_fd = -1;
    job->next = NULL;
    //具体如何实现，主要看这个handler,线程是根据这个hander
    job->handler = job_loop_read_recodingfile;
    job->length = 0;
    job->buffer_size = sizeof(job->buffer);
    job->terminate = 0;
    job->ins = ins_transVideo_create(config);
    
    return job;
}

static void job_destroy(struct Job *job){
    if(job){
        if(job->fd > -1)
            close(job->fd);
        if(job->recording_fd > -1)
            close(job->recording_fd);
        free(job);
        ins_transVideo_delete(job->ins);
        free(job->config);
    }
}


int job_listen_socket_init(short port){
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	fcntl(fd, F_SETFL, O_NONBLOCK);
    
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

    int val = 1;
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&val,sizeof(val));

	if(bind(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("bind");
        close(fd);
        return -1;
    }

	if (listen(fd, 20) < 0) {
		printf("%s:%s\n",__func__,strerror(errno));
        close(fd);
        return (-1);
    }
	return fd;        
}

static int job_is_file_existed(const char*filename){
    if(!filename)
        return 0;
    return (access(filename,F_OK) == 0);
}


static long job_get_file_size(const char *filename){
    if(filename == NULL)
        return 0;
    struct stat st;
    if(stat(filename,&st) != -1){
        return st.st_size;
    }
    return 0;
}

void job_exec(const char *command){
    char *cmd = strdup(command);
    if(cmd == NULL)
        return;

    char *p = strtok(cmd," ");
    char *argv[64];
    int k = 0;
    for(;p!=NULL;p = strtok(NULL," ")){
        argv[k++] = p;//注意，这里指针指向cmd那块内存，argv使用完了才能free cmd
    }
    argv[k++] = NULL;
    
    pid_t pid = fork();
    if(pid < 0){
        perror("fork");
        return ;
    }
    if(pid == 0){
      printf("start call ffmpeg\n");
      int fdnull = open("/dev/null", O_RDWR);
      dup2(fdnull,1);
      dup2(fdnull,2);
   
      if(execvp(argv[0],argv) < 0){
         perror("execvp");
      }
      close(fdnull);    
      free(cmd);
    }else {
        wait(NULL);//must wait
        printf("ffmpeg finished work!\n");
        free(cmd);
    }        
}

static void job_call_ffmpeg_for_video(struct  Job *job){
    if(job == NULL)
        return;

    char command[512];
    
    snprintf(command,512,"/usr/local/bin/ffmpeg -i %s %s",job->config->tmpfile,\
        job->config->outfile); 
    //log
    printf("command:%s\n",command);
    
    if(job_is_file_existed(job->config->tmpfile)){
        
        if(job_get_file_size(job->config->tmpfile) > 0){

            if(job_is_file_existed(job->config->outfile)){

                DELETE_FILE(job->config->outfile);

            }
         //  system(command);//这里需要自己实现exec,不用
            job_exec(command);  

        } 
        DELETE_FILE(job->config->tmpfile);
    }
}

static void *job_worker_thread(void *arg){
    struct  Job *job = (struct Job*)arg;
    job->handler(job);
    job_call_ffmpeg_for_video(job);
    job_destroy(job);
    return 0;
}

static int job_can_recv(int fd,int ms){
    fd_set rfds;
    
    FD_ZERO(&rfds);

    FD_SET(fd,&rfds);

    struct timeval t={
        .tv_sec=ms/1000,
        .tv_usec=(ms%1000)*1000
    };
        
    int rv = select(fd+1,&rfds,NULL,NULL,&t);
    if(rv > 0)
        return 1;
    return 0;
}

static void job_show_config(struct VideoConfig*config){
    printf("show config:\n");
    printf("width:%d\n",config->width);
    printf("height:%d\n",config->height);
    printf("bitrate:%d\n",config->bitrate);
    printf("codec:%s\n",config->codec);
    printf("sid:%s\n",config->sid);
    printf("path:%s\n",config->path);
    printf("outfile:%s\n",config->outfile);
    printf("tmpfile:%s\n",config->tmpfile);
    printf("mode:%s\n",config->mode);
}


static int job_cjson_parse_root(struct VideoConfig*config,cJSON*root){
    cJSON *item = cJSON_GetObjectItem(root,"width");
    if(!item || item->type != cJSON_String){
         return -1;
    }
    config->width = atoi(item->valuestring);

    item = cJSON_GetObjectItem(root,"height");
    if(!item || item->type != cJSON_String){
         return -1;
    }
    config->height = atoi(item->valuestring);

    item = cJSON_GetObjectItem(root,"bitrate");
    if(!item || item->type != cJSON_String){
         return -1;
    }
    config->bitrate = atoi(item->valuestring);

    item = cJSON_GetObjectItem(root,"sid");
    if(!item || item->type != cJSON_String){
         return -1;
    }
    snprintf(config->sid,sizeof(config->sid),"%s",item->valuestring);

    item = cJSON_GetObjectItem(root,"path");
    if(!item || item->type != cJSON_String){
         return -1;
    }

    snprintf(config->path,sizeof(config->path),"%s",item->valuestring);

    item = cJSON_GetObjectItem(root,"mode");
    if(!item || item->type != cJSON_String){
        // return -1;
    }

   // snprintf(config->mode,sizeof(config->mode),"%s",item->valuestring);

    item = cJSON_GetObjectItem(root,"recordingfile");
    
    if(!item || item->type != cJSON_String){
         return -1;
    }

    snprintf(config->recordingfile,sizeof(config->recordingfile),"%s",item->valuestring);
    
    return 0;
}

static int job_recv_video_config(int clifd,struct VideoConfig *config){
    
    if(job_can_recv(clifd, 5000) == 0){
        fprintf(stdout,"Recv video config timeout\n");
        return -1;
    }

    char buffer[2048];
    memset(buffer,0,sizeof(buffer));
    int rv = read(clifd,buffer,sizeof(buffer));
    if(rv > 0){
       fprintf(stdout,"recv:%s\n",buffer);
       cJSON*root = cJSON_Parse(buffer);
       if(!root){
            printf("cJSON_Parse error\n");
            return -1;
       }
       if(job_cjson_parse_root(config,root) < 0){
            printf("parse root failed\n");
            cJSON_Delete(root);
            return -1;
       }
       cJSON_Delete(root);       
    }
    
    //以下是config的其他数据
    //需要检查数据完整性
    snprintf(config->codec,sizeof(config->codec),"mpeg4");
    
    snprintf(config->tmpfile,sizeof(config->tmpfile),"%s/%s.m4v",\
        config->path,config->sid);
    
    snprintf(config->outfile,sizeof(config->outfile),"%s/%s.mp4",\
        config->path,config->sid);
    return 0;
}


static int job_client_callback(int fd, int events, void *arg){

    struct EpollReactor *reactor = (struct EpollReactor*)arg;
    if (reactor == NULL) 
        return -1; 

    char buf[1024];

    int rv = read(fd,buf,sizeof(buf));

    if(rv == 0){
        //job ->terminate = 1;//要求线程断开
       //lock
       struct Job *job = (struct Job*)reactor->events[fd].data;
       if(job){
            job->terminate = 1;//告诉线程可以退出了
       }
       printf("client close:%d\n",job->fd);
       //unlock
       rwEvent_del(reactor->epfd,&reactor->events[fd]);
       close(fd);
    }
    if(rv  > 0){
        buf[rv] = 0;
        printf("data:%s\n",buf);
    }else
        return -1;
    return 0;
}


static void job_connection_handle(struct EpollReactor*reactor,int clientfd){
    if(reactor == NULL){
        return ;
    }
    
    struct VideoConfig *config = (struct VideoConfig *)malloc(sizeof(struct VideoConfig));
    if(job_recv_video_config(clientfd,config) < 0){
        free(config);
        close(clientfd);
        return ;
    }

    //
    job_show_config(config);

    struct Job *job = job_create(config,clientfd);
    job->ptr = reactor->server_inter;
    
    if(pthread_create(&job->thid,NULL,job_worker_thread,job) != 0 ){
        perror("thread_create");
        job_destroy(job);
    }
    else {
        //把job放进链表中
        reactor->events[clientfd].data = job;
        rwEvent_set(&reactor->events[clientfd],clientfd,job_client_callback, reactor);
	    rwEvent_add(reactor->epfd, EPOLLIN, &reactor->events[clientfd]);
    }
}



int job_accept_callback(int fd, int events, void *arg){

    struct EpollReactor *reactor = (struct EpollReactor*)arg;
    if (reactor == NULL) 
        return (-1);
    
    struct sockaddr_in client_addr;

    socklen_t len = sizeof(client_addr);

    int clientfd;

    if ((clientfd = accept(fd, (struct sockaddr*)&client_addr, &len)) == -1) {
        printf("accept: %s\n", strerror(errno));
        return (-1);
    }
    
    printf("new connection<%s:%d>\n",inet_ntoa(client_addr.sin_addr),\
        ntohs(client_addr.sin_port));

    job_connection_handle(reactor,clientfd);

    return 0;
}
