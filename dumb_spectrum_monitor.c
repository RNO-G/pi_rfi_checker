#define _GNU_SOURCE
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h>
#include <libconfig.h> 
#include <pthread.h> 
#include <gps.h> 
#include <unistd.h>
#include <sys/stat.h> 


#define RUN_COUNTER "/data/count"
#define RUN_COUNTER_TMP "/data/count.tmp"
#define DATA_PREFIX "/data" 
#define CONFIG_FILE "/data/config"
#define TMP_DIR "/dev/shm/dumb_spectrum_monitor"


void reset_tmp()
{
  system("rm -rf " TMP_DIR);
  mkdir(TMP_DIR,0777); 
}

static volatile int die = 0; 
pthread_t scanner ;

void handle_deadly(int signal) 
{
  fprintf(stderr,"Stopping from signal %d(might take up to a minute)\n",signal); 
  die = 1; 
}

static pthread_mutex_t scan_mutex = PTHREAD_MUTEX_INITIALIZER; 
static pthread_mutex_t scan_ready = PTHREAD_MUTEX_INITIALIZER; 


void * do_scan(void * cmd) 
{

  while (!die)
  {
    pthread_mutex_lock(&scan_ready); 
    if(system( (const char*) cmd))
    {
      fprintf(stderr,"Trouble running %s. Sleeping for 10 seconds. \n", (const char*) cmd); 
      sleep(10); 
    }
    pthread_mutex_unlock(&scan_mutex); 
  }
  return 0; 

}


int main(int nargs, char ** args) 
{

  //configuration
			
  const char * binary = "rtl_power_fftw"; 
  const char * frequency_range="50M:1650M";
  int nbins = 512; 
  double gain = 10; 
  const char * gpsd_port = "2947"; 

  config_t cfg;
  config_init(&cfg); 
  config_read_file(&cfg,CONFIG_FILE); 
  config_lookup_string(&cfg, "binary", &binary);
  config_lookup_string(&cfg, "gpsd_port", &gpsd_port);
  config_lookup_string(&cfg, "frequency_range", &frequency_range);
  config_lookup_float(&cfg, "gain", &gain);
  config_lookup_int(&cfg, "nbins", &nbins);

  
  int gain_cB = gain*10; 

	//increment the run counter
  int run = 0; 
	FILE * frun = fopen(RUN_COUNTER,"r"); 
	if (frun) fscanf(frun,"%d",&run); 

	fclose(frun); 
  run++; 
	FILE * fruntmp = fopen(RUN_COUNTER_TMP,"w"); 
	fprintf(fruntmp,"%d", run); 
	fclose(fruntmp) ; 
	rename (RUN_COUNTER_TMP,RUN_COUNTER); 
	

  //connect to the GPS daemon and begin streaming
  struct gps_data_t gps; 
  gps_open("localhost",gpsd_port,&gps); 
  gps_stream(&gps, WATCH_ENABLE,0); 

	//make the output directory 
	char * dirname; 
	asprintf(&dirname,DATA_PREFIX "/run_%05d", run); 
  mkdir(dirname,0777); 

  //copy config to it 
  char * copy_cmd; 
  asprintf(&copy_cmd,"cp " CONFIG_FILE " %s/config", dirname); 
  system(copy_cmd);
	free(dirname); 
  free(copy_cmd); 

  //set up signals
  signal(SIGINT, &handle_deadly); 


  //craft the command
  char * cmd; 
  asprintf(&cmd,"%s -q -f %s -b %d -g %d -n 1 -m " TMP_DIR "/scan 2> /dev/null", binary, frequency_range, nbins, gain_cB); 
  printf("Command is %s\n", cmd); 

  reset_tmp(); 
  //start the scanning thread
  pthread_mutex_lock(&scan_mutex); 
  pthread_create(&scanner, NULL, do_scan, cmd); 

  int iscan = 0; 
  while(!die) 
  {
    //set up the gps output file 
    

    FILE * fgps = fopen(TMP_DIR "/gps.csv","w"); 
    fprintf(fgps,"unixtime,fix,gps_time, lat, lon, alt, nsats\n"); 
    while(true) 
    {
      if (!gps_waiting(&gps, 5000000))
      {
        fprintf(fgps, "%ld,-1,-1,-999,-999,-999,-999\n",time(0)); 
      }
      else
      {
        gps_read(&gps); 
        fprintf(fgps,"%ld,%d,%f,%f,%f,%f,%d\n", time(0), gps.fix.mode, gps.fix.time, gps.fix.latitude, gps.fix.longitude, gps.fix.altitude, gps.satellites_visible); 
      }
      //now check to see if the scan thread is done with a scan?
      if (!pthread_mutex_trylock(&scan_mutex))
      {
        pthread_mutex_unlock(&scan_mutex); 
        break; 
      }
    }
    fclose(fgps); 

    char * tar_cmd; 
    asprintf(&tar_cmd, "tar -C "TMP_DIR" --xform=\"s|\\.|.%06d.|\" -czf " DATA_PREFIX "/run_%05d/%06d.tar.gz scan.bin scan.met gps.csv", iscan,run,iscan); 
  //  printf(tar_cmd); 
    system(tar_cmd); 
    reset_tmp(); 
    sync(); 
    free(tar_cmd); 
    iscan++; 
    pthread_mutex_lock(&scan_mutex); 
    pthread_mutex_unlock(&scan_ready); 
  }

  pthread_join(scanner,0); 

  gps_close(&gps); 
  config_destroy(&cfg); 
  free(cmd); 

  return 0; 


}


