void* EchoServe(void* inputs) {
    struct threadInput* threadData = (struct threadInput*)inputs;
    int psd = threadData->psd;
    struct sockaddr_in from = threadData->from;
    char buf[MAX_BUFFER];
    int rc;
    struct  hostent *hp, *gethostbyname();
    
    fprintf(stderr, "Serving %s:%d\n", inet_ntoa(from.sin_addr),
	   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *)&from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	    fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	    fprintf(stderr, "(Name is : %s)\n", hp->h_name);
    
    /**  get data from  clients and send it back */
    for(;;){
	    fprintf(stderr, "\n...server is waiting...\n");
        if(send(psd, " #\n", 2, 0) <0 )
            perror("sending stream message");
        fprintf(stderr,"Sent message...\n");
	    if( (rc=recv(psd, buf, sizeof(buf), 0)) < 0){
	        perror("receiving stream  message");
            threadData->threadComplete = COMPLETE;
	        //exit(-1);
	    }
	    if (rc > 0){
	        buf[rc]='\0';
	        fprintf(stderr, "Received: %s\n", buf);
            char* cmdResult = parseCommand(buf);
	        fprintf(stderr, "From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		    ntohs(from.sin_port));
	        fprintf(stderr,"(Name is : %s)\n", hp->h_name);
            fprintf(stderr,"1::The cmd result: %s and the size is %lu\n", cmdResult, strlen(cmdResult));
            writeLog(cmdResult, inet_ntoa(from.sin_addr),ntohs(from.sin_port));
	        if (send(psd, cmdResult, strlen(cmdResult), 0) <0 )
		        perror("sending stream message");
	    } else {
	        fprintf(stderr, "TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		    ntohs(from.sin_port));
	        fprintf(stderr,"(Name is : %s)\n", hp->h_name);
	        fprintf(stderr, "Disconnected..\n");
	        //close (psd);
            threadData->threadComplete = COMPLETE;
	        return inputs;
	    }
    }
    threadData->threadComplete = COMPLETE;
    return inputs;
} 