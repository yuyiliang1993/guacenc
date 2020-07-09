/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "encode_video.h"

static InsTransVideo_t *g_ptr_transVideo = NULL;

int ins_read_video_configure(struct VideoConfig*config){
    //setup temp data,will read file to set
    config->width = 800;
    config->height = 600;
    config->bitrate = 2000000;
    strcpy(config->codec,"mpeg4");
    config->codec[5] = 0;
    const char*tmp = "/home/workroom1/test.m4v";
    strcpy(config->outfile,tmp);
    config->outfile[strlen(tmp)] = 0;
    return 0;
}

InsTransVideo_t*ins_transVideo_create(struct VideoConfig *config){
    struct InsTransVideo *ins = \
        (struct InsTransVideo*)malloc(sizeof(struct InsTransVideo)*1);
    if(ins == NULL)
        return NULL;
    ins->offsetLen = 0;
    ins->display = NULL;
    ins->parser = NULL;   
    ins->parser = guac_parser_alloc();
    if(access(config->tmpfile,F_OK) == 0)
        unlink(config->tmpfile);
    ins->display = guacenc_display_alloc(config->tmpfile,\
        config->codec,config->width, config->height,config->bitrate);
    if(ins->display == NULL || ins->parser == NULL){
        if(ins->display)
            guacenc_display_free(ins->display);
        if(ins->parser)
            guac_parser_alloc(ins->parser);
        free(ins);
        ins = NULL;
    }
    return ins;
}


void ins_transVideo_delete(InsTransVideo_t *ins){
    if(ins){
        printf("free transvideo\n");
        if(ins->display)
            guacenc_display_free(ins->display);
        if(ins->parser)
            guac_parser_alloc(ins->parser);
        free(ins);
    }
}


//change by yuliang
static void ins_parser_reset(guac_parser* parser) {
    parser->opcode = NULL;
    parser->argc = 0;
    parser->state = GUAC_PARSE_LENGTH;
    parser->__elementc = 0;
    parser->__element_length = 0;
}

static void ins_free_global_memory(void){
    printf("free transVideo_delete\n");
    ins_transVideo_delete(g_ptr_transVideo);
    g_ptr_transVideo = NULL;
}

static void ins_handle_complete_instruction(guacenc_display*display,guac_parser* parser){
    if (guacenc_handle_instruction(display, parser->opcode,
         parser->argc, parser->argv)) {
        guacenc_log(GUAC_LOG_DEBUG, "Handling of \"%s\" instruction "
            "failed.", parser->opcode);
    } 
}

static int ins_read_instructions(InsTransVideo_t*ins,char *dest,int size){
    if(NULL == ins)
        return -1;
    char *buffer = ins->data;
    if(ins->offsetLen == 0){
       return 0;            
    }

    int *length =& ins->offsetLen;
    int rv = 0;
    if(size >= ins->offsetLen){
       memcpy(dest,buffer,*length);
       rv = *length; 
       *length = 0;       
    }
    else {
        memcpy(dest,buffer,size);
        memmove(buffer,buffer+size,*length - size);
        *length -= size;
        rv = size;
    }
    return rv;
}


int ins_parser_read(InsTransVideo_t *ins){
    if(ins == NULL)
        return (-1);
#if 1
    guac_parser* parser = ins->parser;
    if(parser == NULL)
        return (-1);
    char* unparsed_end   = parser->__instructionbuf_unparsed_end;
    char* unparsed_start = parser->__instructionbuf_unparsed_start;
    char* instr_start    = parser->__instructionbuf_unparsed_start;
    char* buffer_end     = parser->__instructionbuf + sizeof(parser->__instructionbuf);

    int loop = 1;
    do{
        if (parser->state == GUAC_PARSE_COMPLETE){
            ins_handle_complete_instruction(ins->display,parser);
            ins_parser_reset(parser);
        }

        /* Add any available data to buffer */
        int parsed = guac_parser_append(parser, unparsed_start, unparsed_end - unparsed_start);
        if(parser->state == GUAC_PARSE_ERROR){
            printf("parer error\n");
            break;
        }
        
        /* Read more data if not enough data to parse */
        if (parsed == 0 && parser->state != GUAC_PARSE_ERROR) {            

            int retval;  
            
            /* If no space left to read, fail */
            if (unparsed_end == buffer_end) {
                /* Shift backward if possible */
                if (instr_start != parser->__instructionbuf) {
                    int i;
                    /* Shift buffer */
                    int offset = instr_start - parser->__instructionbuf;
                    memmove(parser->__instructionbuf, instr_start,
                            unparsed_end - instr_start);

                    /* Update tracking pointers */
                    unparsed_end -= offset;
                    unparsed_start -= offset;
                    instr_start = parser->__instructionbuf;

                    /* Update parsed elements, if any */
                    for (i=0; i < parser->__elementc; i++)
                        parser->__elementv[i] -= offset;

                }

                /* Otherwise, no memory to read */
                else {
                    printf("Instruction too long\n");
                    guac_error = GUAC_STATUS_NO_MEMORY;
                    guac_error_message = "Instruction too long";
                    return -1;
                }

            }
            retval = ins_read_instructions(ins,unparsed_end,buffer_end - unparsed_end);

            if(retval <= 0)
                loop = 0;  
            unparsed_end += retval;
            
        }

        /* If data was parsed, advance buffer */
        else
            unparsed_start += parsed;
    }while(loop);

    /* Fail on error */
    if (parser->state == GUAC_PARSE_ERROR) {
        guac_error = GUAC_STATUS_PROTOCOL_ERROR;
        guac_error_message = "Instruction parse error";
        printf("Instruction parse error\n");
        return -1;
    }
    
    parser->__instructionbuf_unparsed_start = unparsed_start;
    parser->__instructionbuf_unparsed_end = unparsed_end;
#endif    
    return 0;

}


//接口,在guacd直接调用，使用此接口，
//如果使用外置服务器，我们使用
int ins_convert_video(const char *buf,int length){
    static struct VideoConfig conf;
    if(g_ptr_transVideo == NULL){
        printf("g_ptr_transVideo == NULL\n");
        ins_read_video_configure(&conf);
        unlink(conf.outfile);
        g_ptr_transVideo = ins_transVideo_create(&conf);
        if(g_ptr_transVideo == NULL)
            return (-1);
        atexit(ins_free_global_memory);
    }

    int k = 0;
    while(k < length){
        if(g_ptr_transVideo->offsetLen > sizeof(g_ptr_transVideo->data)){
            printf("Too long instructs\n");
            g_ptr_transVideo->offsetLen = 0;
            return -1;
        } 
        g_ptr_transVideo->data[g_ptr_transVideo->offsetLen++] = buf[k++];
        if(g_ptr_transVideo->data[g_ptr_transVideo->offsetLen-1] == ';'){
           ins_parser_read(g_ptr_transVideo);
           g_ptr_transVideo->offsetLen = 0;
        }
    }
    return 0;
}


void ins_test_read(int argc,char **argv){
    if(argc != 2){
        fprintf(stdout,"Usage:guacenc <in_file>\n");
        return ;
    }
    const char *filename = argv[1];
    printf("Start open filename:%s\n",filename);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("%s: %s", filename, strerror(errno));
        return ;
    }

    char buffer[4096];
    int nbytes = 0;
    while(1){   
        nbytes = read(fd,buffer,sizeof(buffer));
        if(nbytes < 0){
            perror("read file");
            break;
        }
        if(nbytes == 0)
            break;
        if(ins_convert_video(buffer,nbytes) < 0)
            break;  
    }
    
    printf("Read completed!\n");
    close(fd);
}

#if 0

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define SERVER_IPADDR "0.0.0.0"
#define SERVER_PORT "1234"
#include <string.h>


int test_net_handle_instructions(){
    int sck = socket(AF_INET,SOCK_STREAM,0);
    if(sck < 0){
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in  addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port = htons(atoi(SERVER_PORT));
    addr.sin_addr.s_addr = inet_addr(SERVER_IPADDR);
    int val = 1;
    setsockopt(sck,SOL_SOCKET,SO_REUSEADDR,&val,sizeof(val));
    
    if(bind(sck,(struct sockaddr*)&addr,sizeof(addr)) < 0){
        perror("bind");
        close(sck);
        return -1;
    }

    listen(sck,0);

    fd_set rfds;
    int max = sck;
    int connfd = -1;
    printf("server start listen:%s:%s\n",SERVER_IPADDR,SERVER_PORT);
 
    while(1){
       // rfds= tmp_rfds;
        FD_ZERO(&rfds);
        FD_SET(sck,&rfds);
        if(connfd > -1)
             FD_SET(connfd,&rfds);
        max = connfd>max?connfd:max;
       
        int n  = select(max+1,&rfds,NULL,NULL,NULL);
        if(n < 0){
            if(errno == EINTR)
                continue;
            perror("select");
            break;
        }
        
        if(n == 0)
            continue;

        if(FD_ISSET(sck,&rfds)){
           struct sockaddr_in cliaddr;
           memset(&cliaddr,0,sizeof(cliaddr));
           socklen_t addrlen = sizeof(cliaddr);
            
           int tmpfd = accept(sck,(struct sockaddr*)&cliaddr,&addrlen);
           if(tmpfd < 0){
                perror("accept");
           }else {
               if(connfd != -1)
                   close(connfd);//关闭旧连接
                printf("accept new connection:%s:%d\n",\
                    inet_ntoa(cliaddr.sin_addr),\
                ntohs(cliaddr.sin_port));
               connfd = tmpfd;
           }
            if(--n <= 0)
                continue;   
        }
        
        if(FD_ISSET(connfd,&rfds)){
            char buf[256];
            int nbytes = read(connfd,buf,sizeof(buf));
            if(nbytes > 0){
                buf[nbytes] = 0;
               // printf("read:%s\n",buf);
                ins_convert_video(buf,nbytes);
            }
            if(nbytes == 0){
                unlink("/home/workroom1/123.avi");
                system("ffmpeg -i /home/workroom1/test.m4v /home/workroom1/123.avi");
                printf("client close\n");
                close(connfd);
                connfd = (-1);
            }
            if(nbytes < 0){
                if(errno == EINTR || errno == EAGAIN);
                else{
                   close(connfd);
                   connfd = (-1);  
                }
            }
            
            if(--n <= 0)
                continue;
        }
       
    }

    close(sck);
    close(connfd);
    return 0;
}
#endif


/**
 * Reads and handles all Guacamole instructions from the given guac_socket
 * until end-of-stream is reached.
 *
 * @param display
 *     The current internal display of the Guacamole video encoder.
 *
 * @param path
 *     The name of the file being parsed (for logging purposes). This file
 *     must already be open and available through the given socket.
 *
 * @param socket
 *     The guac_socket through which instructions should be read.
 *
 * @return
 *     Zero on success, non-zero if parsing of Guacamole protocol data through
 *     the given socket fails.
 */


static int guacenc_read_instructions(guacenc_display* display,
        const char* path, guac_socket* socket) {

    /* Obtain Guacamole protocol parser */
    guac_parser* parser = guac_parser_alloc();
    if (parser == NULL)
        return 1;

    /* Continuously read and handle all instructions */
    
    //从指令文件中读取数据，并且把数据解析到parser中
    while (!guac_parser_read(parser, socket, -1)) {
        if (guacenc_handle_instruction(display, parser->opcode,
                parser->argc, parser->argv)) {
            guacenc_log(GUAC_LOG_DEBUG, "Handling of \"%s\" instruction "
                    "failed.", parser->opcode);
        }
    }
    
    /* Fail on read/parse error */
    if (guac_error != GUAC_STATUS_CLOSED) {
        guacenc_log(GUAC_LOG_ERROR, "%s: %s",
                path, guac_status_string(guac_error));
        guac_parser_free(parser);
        return 1;
    }

    /* Parse complete */
    guac_parser_free(parser);
    return 0;

}



int guacenc_encode(const char* path, const char* out_path, const char* codec,
        int width, int height, int bitrate, bool force) {

    /* Open input file */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        guacenc_log(GUAC_LOG_ERROR, "%s: %s", path, strerror(errno));
        return 1;
    }

    /* Lock entire input file for reading by the current process */
    struct flock file_lock = {
        .l_type   = F_RDLCK,
        .l_whence = SEEK_SET,
        .l_start  = 0,
        .l_len    = 0,
        .l_pid    = getpid()
    };

    /* Abort if file cannot be locked for reading */
    if (!force && fcntl(fd, F_SETLK, &file_lock) == -1) {

        /* Warn if lock cannot be acquired */
        if (errno == EACCES || errno == EAGAIN)
            guacenc_log(GUAC_LOG_WARNING, "Refusing to encode in-progress "
                    "recording \"%s\" (specify the -f option to override "
                    "this behavior).", path);

        /* Log an error if locking fails in an unexpected way */
        else
            guacenc_log(GUAC_LOG_ERROR, "Cannot lock \"%s\" for reading: %s",
                    path, strerror(errno));

        close(fd);
        return 1;
    }


    /* Allocate display for encoding process */
    guacenc_display* display = guacenc_display_alloc(out_path, codec,
            width, height, bitrate);
    if (display == NULL) {
        close(fd);
        return 1;
    }

    /* Obtain guac_socket wrapping file descriptor */
    guac_socket* socket = guac_socket_open(fd);
    if (socket == NULL) {
        guacenc_log(GUAC_LOG_ERROR, "%s: %s", path,
                guac_status_string(guac_error));
        close(fd);
        guacenc_display_free(display);
        return 1;
    }

    guacenc_log(GUAC_LOG_INFO, "Encoding \"%s\" to \"%s\" ...", path, out_path);

    /* Attempt to read all instructions in the file */
    if (guacenc_read_instructions(display, path, socket)) {
        guac_socket_free(socket);
        guacenc_display_free(display);
        return 1;
    }

    /* Close input and finish encoding process */
    guac_socket_free(socket);
    return guacenc_display_free(display);
}
