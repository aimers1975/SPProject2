                    if( (rc=recv(psd, recBuf, sizeof(recBuf), 0)) < 0){
                        perror("receiving stream  message");
                        //TODO: take out?
                        //exit(-1);
                    }  
                    if(rc > 0) {
                        fprintf(stderr," GOT A CONTROL\n");
                        if(strstr(recBuf, "CTL") == &recBuf[0]){
                            if(strstr(recBuf,"c") == &recBuf[4] ) {
                                //Ctrl-c - To stop the current running command (on the server)
                                fprintf(stderr," GOT A CONTROL, sending SIGSTOP to PID: %d\n", ret3);
                                kill(ret3, SIGSTOP);
                            } else if (strstr(recBuf,"z") == &recBuf[4]) {
                                //Ctrl-z  - To suspend the current running command (on the server)
                                kill(ret3, SIGINT);   
                                fprintf(stderr," GOT A CONTROL, sending SIGINT to PID: %d\n", ret3);                            
                            }
                        } 
                        break;                           
                    }