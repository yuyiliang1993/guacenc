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

#include "config.h"

#include "encode.h"
#include "guacenc.h"
#include "log.h"
#include "parse.h"

#include <libavcodec/avcodec.h>

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>

//#include <pthread.h>

//#include "epollreactor.h"
void server_init(server_inter_t *serv,int window_size){
    serv->thread_window_size = window_size;
    sem_init(&serv->sem,0,window_size);
    pthread_mutex_init(&serv->mutex,NULL);
}

void server_destory(server_inter_t *serv){
    sem_destroy(&serv->sem);
    pthread_mutex_destroy(&serv->mutex);
}

void job_exec(char *command);

int main(int argc,char*argv[]){
 #if 0
    unlink("/home/workroom1/1234.mp4");
    job_exec("/usr/local/bin/ffmpeg -i /home/workroom1/123.m4v /home/workroom1/1234.mp4");
    return 0;
 #endif
 
    server_inter_t server_inter;
    server_init(&server_inter,5);
    
    struct EpollReactor reactor;
    epoll_reactor_init(&reactor);
    reactor.server_inter = &server_inter;

    if(!epoll_reactor_listener(&reactor,\
        job_listen_socket_init(SERVER_PORT),job_accept_callback)){
        epoll_reactor_run(&reactor);
    }
    epoll_reactor_destory(&reactor); 
    server_destory(&server_inter);
    return 0;
}

#if 0

int test_net_handle_instructions();

int xymain(int argc, char* argv[]) {
    //test_net_handle_instructions();
   // return 0;
    int i;
    /* Load defaults */
    bool force = false;
    int width = GUACENC_DEFAULT_WIDTH;
    int height = GUACENC_DEFAULT_HEIGHT;
    int bitrate = GUACENC_DEFAULT_BITRATE;

    /* Parse arguments */
    int opt;
    while ((opt = getopt(argc, argv, "s:r:f")) != -1) {

        /* -s: Dimensions (WIDTHxHEIGHT) */
        if (opt == 's') {
            if (guacenc_parse_dimensions(optarg, &width, &height)) {
                guacenc_log(GUAC_LOG_ERROR, "Invalid dimensions.");
                goto invalid_options;
            }
        }

        /* -r: Bitrate (bits per second) */
        else if (opt == 'r') {
            if (guacenc_parse_int(optarg, &bitrate)) {
                guacenc_log(GUAC_LOG_ERROR, "Invalid bitrate.");
                goto invalid_options;
            }
        }

        /* -f: Force */
        else if (opt == 'f')
            force = true;

        /* Invalid option */
        else {
            goto invalid_options;
        }

    }

    /* Log start */
    guacenc_log(GUAC_LOG_INFO, "Guacamole video encoder (guacenc) "
            "version " VERSION);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
    /* Prepare libavcodec */
    avcodec_register_all();
#endif

    /* Track number of overall failures */
    int total_files = argc - optind;
    int failures = 0;

    /* Abort if no files given */
    if (total_files <= 0) {
        guacenc_log(GUAC_LOG_INFO, "No input files specified. Nothing to do.");
        return 0;
    }

    guacenc_log(GUAC_LOG_INFO, "%i input file(s) provided.", total_files);

    guacenc_log(GUAC_LOG_INFO, "Video will be encoded at %ix%i "
            "and %i bps.", width, height, bitrate);

    /* Encode all input files */
    for (i = optind; i < argc; i++) {

        /* Get current filename */
        const char* path = argv[i];

        /* Generate output filename */
        char out_path[4096];
        int len = snprintf(out_path, sizeof(out_path), "%s.m4v", path);

        /* Do not write if filename exceeds maximum length */
        if (len >= sizeof(out_path)) {
            guacenc_log(GUAC_LOG_ERROR, "Cannot write output file for \"%s\": "
                    "Name too long", path);
            continue;
        }

        /* Attempt encoding, log granular success/failure at debug level */
        if (guacenc_encode(path, out_path, "mpeg4",
                    width, height, bitrate, force)) {
            failures++;
            guacenc_log(GUAC_LOG_DEBUG,
                    "%s was NOT successfully encoded.", path);
        }
        else
            guacenc_log(GUAC_LOG_DEBUG, "%s was successfully encoded.", path);

    }

    /* Warn if at least one file failed */
    if (failures != 0)
        guacenc_log(GUAC_LOG_WARNING, "Encoding failed for %i of %i file(s).",
                failures, total_files);

    /* Notify of success */
    else
        guacenc_log(GUAC_LOG_INFO, "All files encoded successfully.");

    /* Encoding complete */
    return 0;

    /* Display usage and exit with error if options are invalid */
invalid_options:

    fprintf(stderr, "USAGE: %s"
            " [-s WIDTHxHEIGHT]"
            " [-r BITRATE]"
            " [-f]"
            " [FILE]...\n", argv[0]);

    return 1;

}
#endif
