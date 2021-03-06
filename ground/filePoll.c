#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

void intHandler(int dummy){
  printf("Exiting...\n");
  exit(0);
}

int main(){
  char command[128];
  int i = 0;
  FILE *fp;
  char p;
  // register intHandler as sigint handler
  signal(SIGINT,intHandler);
  // remove all .tmp files
  // create command
  snprintf(command,64,"rm -f /downlinkStaging/*.tmp");
  // execute rm
  fp = popen(command,"r");
  // error checking
  if(fp == NULL){
    printf("Popen error\n");
    return -1;
  }
  // 'forward' stdout
  //while((p=fgetc(fp))!=EOF){
    //printf("%c",p);
  //}
  // wait
  pclose(fp);
  // continuously monitor for files
  while(1){
    // create command
    snprintf(command,128,"/home/pi/GitRepos/Lepton-Balloon/ground/getfile -o /downlinkStaging/%d.tmp",i);
    // execute getFile
    fp = popen(command,"r");
    // error checking
    if(fp == NULL){
      printf("Popen error\n");
      return -1;
    }
    // 'forward' stdout
    //while((p=fgetc(fp))!=EOF){
     //printf("%c",p);
    //}
    // wait for file
    pclose(fp);
    // increment file counter
    i++;
    // wait a bit
    //usleep(1000);
  }
}
